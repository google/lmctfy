// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Command handling logic.

#include "lmctfy/cli/command.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <sstream>
#include <string>
using ::std::string;
#include <vector>

#include "gflags/gflags.h"
#include "base/logging.h"
#include "strings/stringpiece.h"
#include "util/gtl/lazy_static_ptr.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

DEFINE_bool(lmctfy_recursive, false,
            "Whether to apply the command recursively to all subcontainers");
DEFINE_bool(lmctfy_force, false,
            "Whether to force the application of the command");
DEFINE_string(lmctfy_config, "",
              "The path to the container configuration to use. This config "
              "includes a single ContainerSpec proto");
DEFINE_bool(lmctfy_no_wait, false, "Whether to wait for the command to exit");
DEFINE_bool(lmctfy_binary, false,
            "Whether to output the command's proto output in binary form");

using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;
using ::util::gtl::LazyStaticPtr;

#define ERROR LOGLEVEL_ERROR

namespace containers {
namespace lmctfy {
namespace cli {

// TODO(vmarmol): Replace global flags with something else which is
// command-specific.
// TODO(vmarmol): Consider making proto and protobinary output types.

// This is the global list of registered commands.
static LazyStaticPtr<CommandVector> root_commands;

// Gets the root command vector.  This is for testing.
const CommandVector *internal::GetRootCommands() {
  return root_commands.get();
}

// Helper: recursively clear commands.
static void ClearCommands(CommandVector *commands) {
  for (const auto &cmd : *commands) {
    if (cmd.type == CMD_TYPE_SUBCMD) {
      ClearCommands(cmd.subcommands);
      delete cmd.subcommands;
    }
  }
}

// Clears the root command vector.  This is for testing.
void internal::ClearRootCommands() {
  ClearCommands(root_commands.get());
  root_commands->clear();
}

// Registers a set of commands.
void RegisterRootCommand(const Command &new_command) {
  // Insert in order.
  CommandVector::iterator it;
  for (it = root_commands->begin(); it != root_commands->end(); ++it) {
    int cmp = strcmp(new_command.name, it->name);
    // Did we collide?
    if (cmp == 0) {
      // Ignore the attempt to add this already-existing command tree.
      // TODO(thockin): Make this FATAL.
      return;                                                    // COV_NF_LINE
    }
    // Did we find the right place?
    if (cmp < 0) {
      break;
    }
  }
  // If we get here, then we either found the right spot or it == end().
  // Insert the new command before it.
  root_commands->insert(it, new_command);
}

// Consumes a line of the specified text up to a limit of characters. Returns
// the consumed line (minus the space at which the line was broken if one
// exists). Lines are broken at spaces, or limit when no spaces are available.
string ConsumeLine(string *text, int limit) {
  // If less than the limit, consume the whole string.
  if (text->size() <= limit) {
    string current_line = *text;
    text->clear();
    return current_line;
  }

  // The current text is everything before the last space (last being before or
  // at the limit). If the text is longer than the limit, just truncate at the
  // limit.
  size_t last_space = text->find_last_of(' ', limit);
  size_t cutoff = last_space != string::npos ? last_space : limit;
  string current_line = text->substr(0, cutoff);

  // Erase the current_line (and the space).
  text->erase(0, cutoff + 1);

  return current_line;
}

// Display the command information with description and overflow to a new line
// when necessary.
void PrintCommand(FILE *out, const Command &command) {
  const int kColumnLimit = 80;
  const int kNameColumns = 22;
  const int kDescriptionColumns = kColumnLimit - kNameColumns;
  string description = command.description;

  // Print command and first line
  string line = ConsumeLine(&description, kDescriptionColumns);
  fprintf(out, "    %-*s  %s\n", kNameColumns - 6, command.name, line.c_str());

  // Print lines as long as we have more of the description to print.
  while (!(line = ConsumeLine(&description, kDescriptionColumns)).empty()) {
    fprintf(out, "%*s%s\n", kNameColumns, "", line.c_str());
  }
}

// Prints command help for a particular menu.
void PrintUsage(FILE *out, const CommandVector *commands) {
  DCHECK(out);
  if (commands == nullptr) {
    commands = root_commands.get();
  }

  string progname = ::gflags::ProgramInvocationShortName();

  fprintf(out, "Usage: %s [command]\n", progname.c_str());
  fprintf(
      out,
      "\n"
      "  Common Global Flags:\n"
      "    -V                Print lmctfy version.\n"
      "    -h                Print lmctfy help.\n"
      "    -l                Set the data output style to long\n"
      "    -p                Set the data output style to pairs [Default]\n"
      "    -v                Set the data output style to values\n"
      "\n"
      "  Common Command-Specific Flags:\n"
      "    -c                Path to container config file to use.\n"
      "    -f                Force the application of the action.\n"
      "    -r                Also apply action to all subcontainers.\n"
      "\n"
      "  Commands:\n");

  for (const auto &cmd : *commands) {
    PrintCommand(out, cmd);
  }
}

// Prints command help for a particular command.  Exposed for testing.
void internal::PrintCommandHelp(FILE *out, const Command *command,
                                const string &command_path) {
  DCHECK(out);
  DCHECK(command);
  fprintf(out, "usage: %s %s\n", command_path.c_str(), command->arguments);
}

// Finds a command in a CommandVector.  Exposed for testing.
const Command *internal::FindCommand(const CommandVector *commands,
                                     const string &name) {
  DCHECK(commands);
  CommandVector::const_iterator it;
  for (const auto &cmd : *commands) {
    if (cmd.name == name) {
      return &cmd;
    }
  }
  return nullptr;
}

// Finds and runs a command.
Status RunCommand(const vector<string> &args, OutputMap::Style output_style,
                  ContainerApiFactory *lmctfy_factory, FILE *out) {
  CHECK_GT(args.size(), 0);

  string command_path = ::gflags::ProgramInvocationShortName();

  const CommandVector *commands = root_commands.get();

  int argc = args.size();
  vector<string>::const_iterator argv = args.begin();
  argc--;
  argv++;
  while (argv != args.end()) {
    const Command *command = internal::FindCommand(commands, *argv);
    if (command == nullptr) {
      break;
    }
    command_path += " ";
    command_path += *argv;

    // If it was a leaf command, run it.
    if (command->type == CMD_TYPE_GETTER || command->type == CMD_TYPE_SETTER ||
        command->type == CMD_TYPE_INIT) {
      // All commands have a "help" argument.
      if (argc >= 2 && *(argv+1) == "help") {
        internal::PrintCommandHelp(out, command, command_path);
        return Status::OK;
      }

      Status status = Status::OK;

      // Check number of arguments.
      int num_arguments = argc - 1;
      if (num_arguments < command->min_num_arguments) {
        status = Status(::util::error::INVALID_ARGUMENT, "Missing arguments");
      } else if (command->max_num_arguments >= 0 &&
                 num_arguments > command->max_num_arguments) {
        status = Status(::util::error::INVALID_ARGUMENT,
                        "Extraneous arguments");
      }

      // Check if there were any errors so far.
      if (!status.ok()) {
        fprintf(stderr, "%s\n", status.error_message().c_str());
        internal::PrintCommandHelp(stderr, command, command_path);
        return status;
      }

      // Create the lmctfy object except for the initialization commands.
      unique_ptr<ContainerApi> lmctfy;
      if (command->type != CMD_TYPE_INIT) {
        StatusOr<ContainerApi *> statusor = lmctfy_factory->Run();
        if (!statusor.ok()) {
          fprintf(stderr,
                  "Failed to create a lmctfy instance with error '%s'\n",
                  statusor.status().error_message().c_str());
          return statusor.status();
        }
        lmctfy.reset(statusor.ValueOrDie());
      }

      // Run the command.
      LOG(INFO) << "Running command: " << command_path;
      OutputMap output;
      status = command->function(vector<string>(argv, args.end()),
                                 lmctfy.get(), &output);
      if (!status.ok()) {
        fprintf(stderr, "Command exited with error message: %s\n",
                status.ToString().c_str());
        if (FLAGS_stderrthreshold >= ERROR) {
          fprintf(stderr, "try using --stderrthreshold to get more info\n");
        }

        return status;
      }

      // Print command's output.
      output.Print(out, output_style);

      return Status::OK;
    }

    // This command must be a sub-menu.  Advance the args and keep looking.
    argc--;
    argv++;
    commands = command->subcommands;
  }

  // If we get here, we did not find a command. Print usage info.
  FindPartialCommandAndPrintUsage(stderr, args);
  return Status(::util::error::NOT_FOUND, "No command found");
}

void FindPartialCommandAndPrintUsage(FILE *out, const vector<string> &args) {
  DCHECK_GT(args.size(), 0);

  const CommandVector *commands = root_commands.get();

  vector<string>::const_iterator argv = args.begin();
  argv++;
  while (argv != args.end()) {
    const Command *command = internal::FindCommand(commands, *argv);
    if (command == nullptr) {
      break;
    }

    if (command->type == CMD_TYPE_GETTER || command->type == CMD_TYPE_SETTER ||
        command->type == CMD_TYPE_INIT) {
      LOG(FATAL) << "this should only be called when a command is NOT found";
      break;
    }

    argv++;
    commands = command->subcommands;
  }

  PrintUsage(out, commands);
}

static void PrintCommandTreeInternal(FILE *out, const CommandVector *commands,
                                     int indent) {
  for (const auto &cmd : *commands) {
    for (int i = 0; i < indent; i++) {
      fprintf(out, "    ");
    }

    fprintf(out, "%s\n", cmd.name);
    if (cmd.type == CMD_TYPE_SUBCMD) {
      PrintCommandTreeInternal(out, cmd.subcommands, indent+1);
    }
  }
}

template<typename T>
string CatenateVector(const vector<T> &vec, const string &delimiter) {
  if (vec.empty()) {
    return "";                                                   // COV_NF_LINE
  }
  std::ostringstream oss;
  oss << vec[0];
  for (size_t i = 1; i < vec.size(); i++) {
    oss << delimiter << vec[i];
  }
  return oss.str();
}

static void PrintCommandTreeLongInternal(FILE *out,
                                         const CommandVector *commands,
                                         vector<int> *node_num_path,
                                         vector<string> *node_name_path) {
  DCHECK(commands);
  DCHECK(node_num_path);
  DCHECK(node_name_path);

  int cur_node_num = 0;

  for (const auto &cmd : *commands) {
    for (size_t i = 0; i < node_num_path->size(); i++) {
      fprintf(out, "    ");
    }

    node_num_path->push_back(cur_node_num);
    node_name_path->push_back(cmd.name);

    if (cmd.type == CMD_TYPE_SUBCMD) {
      const char *type;
      if (node_num_path->size() == 1) {
        type = "root";
      } else {
        type = "branch";
      }
      fprintf(out, "[%s %s] %s\n", type,
              CatenateVector(*node_num_path, ":").c_str(),
              CatenateVector(*node_name_path, " ").c_str());
      PrintCommandTreeLongInternal(out, cmd.subcommands,
                                   node_num_path, node_name_path);
    } else if (cmd.type == CMD_TYPE_GETTER) {
      fprintf(out, "[leaf %s] %s\n",
              CatenateVector(*node_num_path, ":").c_str(),
              CatenateVector(*node_name_path, " ").c_str());
    } else if (cmd.type == CMD_TYPE_SETTER) {
      fprintf(out, "[flur %s] %s\n",
              CatenateVector(*node_num_path, ":").c_str(),
              CatenateVector(*node_name_path, " ").c_str());
    } else if (cmd.type == CMD_TYPE_INIT) {
      fprintf(out, "[init %s] %s\n",
              CatenateVector(*node_num_path, ":").c_str(),
              CatenateVector(*node_name_path, " ").c_str());
    }

    node_num_path->pop_back();
    node_name_path->pop_back();
    cur_node_num++;
  }
}

void PrintCommandTree(FILE *out, const CommandVector *commands) {
  if (commands == nullptr) {
    commands = root_commands.get();
  }
  PrintCommandTreeInternal(out, commands, 0);
}

void PrintCommandTreeLong(FILE *out, const CommandVector *commands) {
  if (commands == nullptr) {
    commands = root_commands.get();
  }
  vector<int> node_num_path;
  vector<string> node_name_path;
  node_name_path.push_back("lmctfy");
  PrintCommandTreeLongInternal(out, commands, &node_num_path, &node_name_path);
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
