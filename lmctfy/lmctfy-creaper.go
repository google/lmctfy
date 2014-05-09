/*
lmctfy-creaper is used to create a lmctfy container, optionally run a command
inside it and then automatically destroy it once the init process in that
container exits. lmctfy-creaper should be started as a background process and
it will exit as soon as the init process in a container that it created exits.
*/
package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"syscall"
)

// containerSpecInput wraps two possible ContainerSpec inputs to lmctfy which
// is either via the command line as a string or through a config file.
type containerSpec struct {
	commandLine string
	specFile    string
}

const (
	// prctl syscall option to set the current process as the parent for all
	// its grand children that gets abandoned.
	prSetChildSubreaper = 36
)

var (
	specFile = flag.String("specFile", "", "Path to a file that contains the Lmctfy ContainerSpec. "+
		"Use either -containerSpec flag or specify the ContainerSpec in the command line.")
	debug         = flag.Bool("debug", false, "When set to true, creaper will print debug information to the stdout.")
	networkSetup  = flag.String("networkSetup", "", "Network setup script called from outside the namespace after namespace and veths are created.")
	lmctfyBinPath = flag.String("lmctfyPath", "/usr/local/bin/lmctfy", "Path to lmctfy binary.")
)

// destroyContainer uses lmctfy internally to unconditionally destroy the
// container referred to by 'containerName'. Logs fatal if container deletion fails.
func destroyContainer(containerName string) {
	cmd := exec.Command(*lmctfyBinPath, "destroy", containerName, "-f")
	if *debug {
		fmt.Println(strings.Join(cmd.Args, " "))
	}
	if out, err := cmd.CombinedOutput(); err != nil {
		log.Printf("failed to destroy container %s. lmctfy output: %s. error: %s",
			containerName, string(out), err)
	}
}

// createContainer uses lmctfy to create a container with the given name and
// lmctfy spec. Returns the output of lmctfy process.
func createContainer(containerName string, spec containerSpec) (string, error) {
	readPipe, writePipe, err := os.Pipe()
	if err != nil {
		return "", err
	}
	defer readPipe.Close()
	// output fd is 3 because writePipe is the first entry in exec.Cmd.ExtraFiles
	args := []string{*lmctfyBinPath, "create", containerName, "--lmctfy_output_fd=3"}

	if spec.specFile != "" {
		args = append(args, "-c", spec.specFile)
	} else {
		args = append(args, spec.commandLine)
	}
	cmd := exec.Command(args[0], args[1:]...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	cmd.ExtraFiles = []*os.File{writePipe}
	if *debug {
		fmt.Println(strings.Join(cmd.Args, " "))
	}
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("container creation failed. error: %s", err)
	}
	writePipe.Close()
	out, err := ioutil.ReadAll(readPipe)
	if err != nil {
		return "", err
	}
	return string(out), nil
}

// runCommandInContainer runs 'userCommand' in container with name 'containerName'.
// Returns error on failure.
func runCommandInContainer(containerName string, userCommand []string) error {
	cmd := exec.Command(*lmctfyBinPath, "run", containerName, strings.Join(userCommand, " "))
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if *debug {
		fmt.Println(strings.Join(cmd.Args, " "))
	}
	err := cmd.Run()
	if err != nil {
		return fmt.Errorf("failed to run command %s in container %s. error: %s",
			strings.Join(userCommand, " "), containerName, err)
	}
	return nil
}

// Extracts the value with key init_pid from lmctfyOutput. Returns the
// value on success and returns an error on failure.
func extractInitPID(lmctfyOutput string) (int, error) {
	// Lmctfy output is in the form of key value pairs (key="value").
	// We are looking for key 'init_pid'
	for _, keyValue := range strings.Split(lmctfyOutput, " ") {
		fields := strings.Split(keyValue, "=")
		if len(fields) != 2 {
			return 0, fmt.Errorf("invalid lmctfy output %s", lmctfyOutput)
		}
		if strings.Contains(fields[0], "init_pid") {
			value := strings.TrimRight(strings.Replace(fields[1], "\"", "", -1), "\n")
			initPID, err := strconv.Atoi(value)
			if err != nil {
				return 0, fmt.Errorf("%s\ninvalid Lmctfy output %s", err, lmctfyOutput)
			}
			return initPID, nil
		}
	}
	return 0, fmt.Errorf("invalid lmctfy output %s", lmctfyOutput)
}

// setChildSubreaper marks the current process to become the parent of any
// grandchildren processes that might be abandoned by its parent
// process. Returns error on failure, nil otherwise.
func setChildSubreaper() error {
	if _, _, errno := syscall.Syscall6(syscall.SYS_PRCTL, uintptr(prSetChildSubreaper),
		uintptr(1), uintptr(0), uintptr(0), uintptr(0), uintptr(0)); errno != 0 {
		return fmt.Errorf("failed to make current process parent of all "+
			"child processes that might be abandoned in the future. errno: %d\n", errno)
	}
	return nil
}

// waitPID waits for 'wpid' to exit and prints the exit status of
// 'wpid'. Returns error on failure, nil otherwise.
func waitPid(wpid int) error {
	var wstatus syscall.WaitStatus
	if *debug {
		log.Printf("Waiting for pid %d to exit", wpid)
	}
	if out, err := syscall.Wait4(wpid, &wstatus, 0, nil); out != wpid {
		return fmt.Errorf("failed while waiting for process %d to exit. error: %s", wpid, err)
	}
	return nil
}

func runNetworkSetup() error {
	if len(*networkSetup) == 0 {
		return nil
	}
	if *debug {
		fmt.Printf("Running network setup command: %s\n", *networkSetup)
	}
	if out, err := exec.Command("/bin/bash", "-c", *networkSetup).CombinedOutput(); err != nil {
		return fmt.Errorf("network setup command: %s, failed with output: %s and error: %s", *networkSetup, out, err)
	}
	return nil
}

// runCreaper does the following:
// 1. creates the container with name 'containerName' and based on 'containerSpec'.
// 2. waits for the init process in the container to exit.
// 3. Runs User specified command 'userCommand' if specified.
// 4. Destroys container once the init process in the container exits or if it
// encounters any failure after creating the container.
// Returns error if any of the steps fail.
func runCreaper(containerName string, cSpec containerSpec, userCommand []string) error {
	if err := setChildSubreaper(); err != nil {
		log.Fatal(err)
	}
	defer destroyContainer(containerName)
	// TODO(vishnuk): Error out if the container already exists.
	lmctfyOutput, err := createContainer(containerName, cSpec)
	if err != nil {
		return err
	}
	initPID, err := extractInitPID(lmctfyOutput)
	if err != nil {
		return err
	}
	if err := runNetworkSetup(); err != nil {
		return err
	}
	message := make(chan error, 1)
	go func() {
		message <- waitPid(initPID)
	}()
	if len(userCommand) > 0 {
		go func() {
			message <- runCommandInContainer(containerName, userCommand)
		}()
	}
	return <-message
}

// parseInput parses args and specFile and returns containerName, containerSpec and user command, if specified.
// Returns error if input is invalid or any internal operation fails.
func parseInput(args []string, specFile string) (string, containerSpec, []string, error) {
	if len(args) == 0 {
		flag.Usage()
		return "", containerSpec{}, []string{}, fmt.Errorf("No args provided")
	}
	containerName := args[0]
	var userCommand []string
	var cSpec containerSpec
	if len(specFile) > 0 {
		if _, err := os.Stat(specFile); err != nil {
			return "", containerSpec{}, []string{},
				fmt.Errorf("cannot open config file %s. %s", specFile, err)
		}
		cSpec.specFile = specFile
		if len(args) > 1 {
			userCommand = args[1:]
		}
	} else {
		cSpec.commandLine = args[1]
		if len(args) > 2 {
			userCommand = args[2:]
		}
	}
	return containerName, cSpec, userCommand, nil
}

// main validates input, invokes runCreaper and exits with error code '1' if
// runCreaper returns error.
func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: lmctfy-creaper <containerName> {<containerSpec> | -specFile} [-networkSetup] [<command>]\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "  <containerName>: A Lmctfy container name.\n"+
			"  <containerSpec>: Lmctfy Container spec.\n"+
			"  <command>: An optional command to be run inside the container.\n"+
			"  Note: The command will be killed as soon as the init process in the container virtual host exits.\n"+
			"  Refer to include/lmctfy.proto for more information\n")
	}
	flag.Parse()
	containerName, cSpec, userCommand, err := parseInput(flag.Args(), *specFile)
	if err != nil {
		log.Fatal(err)
	}
	if err := runCreaper(containerName, cSpec, userCommand); err != nil {
		log.Fatal(err)
	}
}
