# lmctfy - Let Me Contain That For You

## Note
We have been collaborating with [Docker](http://docker.com) over libcontainer and are in process of porting the core lmctfy concepts and abstractions to [libcontainer](http://github.com/docker/libcontainer). We are not actively developing lmctfy further and have moved our efforts to libcontainer. In future, we hope to replace the core of lmctfy with libcontainer.

## Introduction
lmctfy (pronounced *l-m-c-t-fi*, IPA: /ɛlɛmsitifаɪ/) is the open source version of [Google](http://google.com)’s container stack, which provides Linux application containers. These containers allow for the isolation of resources used by multiple applications running on a single machine. This gives the applications the impression of running exclusively on a machine. The applications may be container-aware and thus be able to create and manage their own subcontainers.

The project aims to provide the container abstraction through a high-level API built around user intent. The containers created are themselves container-aware within the hierarchy and can be delegated to be managed by other user agents.

lmctfy was designed and implemented with specific use-cases and configurations in mind and may not work out of the box for all use-cases and configurations. We do aim to support more use-cases and configurations so please feel free to [contribute](#contributing) patches or send e-mail to the [mailing list](#mailing-list) so that we may incorporate these into the [roadmap](#roadmap).

lmctfy is released as both a C++ library and a CLI.

## Current Status
lmctfy is currently stalled as we migrate the core concepts to libcontainer and build a standard container management library that can be used by many projects.

lmctfy is beta software and may change as it evolves. The latest release version is `0.5.0`. It currently provides isolation for CPU, memory, and devices. It also allows for the creation of Virtual Hosts which are more heavily isolated containers giving the impression of running as an independent host. 

## Getting Started
This section describes building the CLI, running all unit tests, and initializing the machine. The [CLI Commands](#cli-commands) section provides some examples of CLI operations and [C++ Library](#c-library) describes the use of the underlying library.
### Dependencies
lmctfy depends on the following libraries and expects them to be available on the system:
* [Protocol Buffers](https://code.google.com/p/protobuf/)
* [gflags](https://code.google.com/p/gflags/) (version >= 2.1.1)
* [RE2](https://code.google.com/p/re2/)
* [AppArmor](http://packages.ubuntu.com/precise/libapparmor-dev)
* glibc (version >= 2.14)

Addtionally to build lmctfy you also need:
* make
* go compiler
* g++ or clang version with C++11 support (tested with g++-4.7 and clang-3.2)

We've tested the setup on **Ubuntu 12.04+**. We are happy to accept patches that add support for other setups.

### Building the CLI
To build the `lmctfy` CLI:

```bash
make -j <number of threads> lmctfy
```

The CLI should now be available at: `bin/lmctfy/cli/lmctfy`
### Building the C++ Library
To build the lmctfy library:

```bash
make -j <number of threads> liblmctfy.a
```

The library should now be available at: `bin/liblmctfy.a`.
### Running Unit Tests

To build and run all unit tests:

```bash
make -j <number of threads> check
```

### Initialization
lmctfy has been tested on **Ubuntu 12.04+** and on the **Ubuntu 3.3** and **3.8** kernels. lmctfy runs best when it owns all containers in a machine so it is not recommended to run lmctfy alongside [LXC](http://lxc.sourceforge.net/) or another container system (although given some configuration, it can be made to work).

In order to run lmctfy we must first initialize the machine. This only needs to happen once and is typically done when the machine first boots. If the cgroup hierarchies are already mounted, then an empty config is enough and lmctfy will auto-detect the existing mounts:

```bash
lmctfy init ""
```

If the cgroup hierarchies are not mounted, those must be specified so that lmctfy can mount them. The current version of lmctfy needs the following cgroup hierarchies: `cpu`, `cpuset`, `cpuacct`, `memory`, and `freezer`. `cpu` and `cpuacct` are the only hierarchies that can be co-mounted, all other must be mounted individually. For details on configuration specifications take a look at `InitSpec` in [lmctfy.proto](/include/lmctfy.proto). An example configuration mounting all of the hierarchies in `/sys/fs/cgroup`:

```bash
lmctfy init "
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/cpu'
    hierarchy:CGROUP_CPU hierarchy:CGROUP_CPUACCT
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/cpuset' hierarchy:CGROUP_CPUSET
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/freezer' hierarchy:CGROUP_FREEZER
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/memory' hierarchy:CGROUP_MEMORY
  }"
```

The machine should now be ready to use `lmctfy` for container operations.

## Container Names
Container names mimic filesystem paths closely since they express a hierarchy of containers (i.e.: containers can be inside other containers, these are called **subcontainers** or **child containers**).

Allowable characters for container names are:
* Alpha numeric (`[a-zA-Z0-9]+`)
* Underscores (`_`)
* Dashes (`-`)
* Periods (`.`)

An absolute path is one that is defined from the root (`/`) container (i.e.: `/sys/subcont`). Container names can also be relative (i.e.: `subcont`). In general and unless otherwise specified, regular filesystem path rules apply.

### Examples:
```
   /           : Root container
   /sys        : the "sys" top level container
   /sys/sub    : the "sub" container inside the "sys" top level container
   .           : the current container
   ./          : the current container
   ..          : the parent of the current container
   sub         : the "sub" subcontainer (child container) of the current container
   ./sub       : the "sub" subcontainer (child container) of the current container
   /sub        : the "sub" top level container
   ../sibling  : the "sibling" child container of the parent container
```


## CLI Commands
### Create
To create a container run:

```bash
lmctfy create <name> <specification>
```

Please see [lmctfy.proto](/include/lmctfy.proto) for the full `ContainerSpec`.

Example (create a memory-only container with `100MB` limit):

```bash
lmctfy create memory_only "memory:{limit:100000000}"
```

### Destroy
To destroy a container run:

```bash
lmctfy destroy <name>
```

### List
To list all containers in a machine, ask to recursively list from root:

```bash
lmctfy list containers -r /
```

You can also list only the current subcontainers:

```bash
lmctfy list containers
```

### Run
To run a command inside a container run:

```bash
lmctfy run <name> <command>
```

Examples:

```bash
lmctfy run test "echo hello world"
lmctfy run /test/sub bash
lmctfy run -n /test "echo hello from a daemon"
```

### Other
Use `lmctfy help` to see the full command listing and documentation.

## C++ Library
The library is comprised of the `::containers::lmctfy::ContainerApi` factory which creates, gets, destroys, and detects `::containers::lmctfy::Container` objects that can be used to interact with individual containers. Full documentation for the lmctfy C++ library can be found in [lmctfy.h](/include/lmctfy.h). 

## Roadmap

The lmctfy project proposes a containers stack with two major layers we’ll call CL1 and CL2. CL1 encompases the driver and enforcement of the containers policy set by CL2. CL1 will create and maintain the container abstraction for higher layers. It should be the only layer that directly interacts with the kernel to manage containers. CL2 is what develops and sets container policy, it uses CL1 to enforce the policy and manage containers. For example: CL2 (a daemon) implements a policy that the amount of CPU and memory used by all of a machine’s containers must not exceed the amount of available CPU and memory (as opposed to overcommitting memory in the machine). To enforce that policy it uses CL1 (library/CLI) to create containers with memory limits that add up to the machine’s available memory. Alternate policies may involve overcommitting a machine’s resources by X% or creating levels of resources with different guarantees for quality of service.

The lmctfy project currently provides the CL1 component. The CL2 is not yet implemented.

### CL1
Currently only provides robust CPU and memory isolation.  In our roadmap we have support for the following:
* *Disk IO Isolation:* The specification is mostly complete, we’re missing the controller and resource handler.
* *Network Isolation:* The specification and cgroup implementation is up in the air.
* *Support for Root File Systems:* Specifying and building root file systems.
* *Disk Images:* Being able to import/export a container’s root file system image.
* *Checkpoint Restore:* Being able to checkpoint and restore containers on different machines.

### CL2
The most basic CL2 would use a container policy that ensures the fair sharing of a machine’s resources without allowing overcommitment. We aim to eventually implement a CL2 that provides different levels of guaranteed quality of service. In this scheme some levels are given stronger quality of service than others. The following CL2 features are supported in our roadmap:
* Monitoring and statistics support.
* Admission control and feasibility checks.
* Quality of Service guarantees and enforcement.

We have started work on CL2 under the [cAdvisor](https://github.com/google/cadvisor/) project

## Kernel Support

lmctfy was originally designed and implemented around a custom kernel with a set of patches on top of a vanilla Linux kernel. As such, some features work best in conjunction with those kernel patches. However, lmctfy should work without them. It should detect available kernel support and adapt accordingly. We’ve tested lmctfy in vanilla **Ubuntu 3.3*** and **3.8** kernels. Please report any issues you find with other kernel versions.

Some of the relevant kernel patches:
* *CPU latency:* This adds the `cpu.lat` cgroup file to the cpu hierarchy. It bounds the CPU wakeup latency a cgroup can expect.
* *CPU histogram accounting:* This adds the `cpuacct.histogram` cgroup file to the cpuacct hierarchy. It provides various histograms of CPU scheduling behavior.
* *OOM management:* Series of patches to enforce priorities during out of memory conditions.

## Contributing

Interested in contributing to the project? Feel free to send a patch or take a look at our [roadmap](#roadmap) for ideas on areas of contribution. Follow [Getting Started](#getting-started) above and it should get you up and running. If not, let us know so we can help and improve the instructions. There is some documentation on the structure of lmctfy in the [primer](/PRIMER.md).

## Mailing List

The project mailing list is <lmctfy@googlegroups.com>. The list will be used for announcements, discussions, and general support. You can [subscribe via groups.google.com](https://groups.google.com/forum/#!forum/lmctfy).
