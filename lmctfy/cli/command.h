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

// Command support logic.  This allows self-contained command modules to
// register themselves and be linked into the lmctfy tool.

#ifndef SRC_CLI_COMMAND_H_
#define SRC_CLI_COMMAND_H_

#include <stdio.h>
#include <initializer_list>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "base/logging.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {


namespace cli {

// A vector of command.  We don't use a vector of pointers here because
// (believe it or not) benchmarking showed that pointers are significantly
// slower than non-pointers when building and walking the command tree.
// Benchmarks also showed vector<> to be faster than list<> here. Possibly a
// compiler return value optimization?
struct Command;

typedef ::std::vector<Command> CommandVector;

// A command action.
typedef ::util::Status (*CommandFunction)(const ::std::vector<string> &argv,
                                          const ContainerApi *lmctfy,
                                          OutputMap *output);

// Differentiate commands from sub-menus.
enum CommandType {
  CMD_TYPE_GETTER,              // Read something, no side effects.
  CMD_TYPE_SETTER,              // Change or do something.
  CMD_TYPE_INIT,                // Performs initialization, do not provide a
                                // lmctfy instance.
  CMD_TYPE_SUBCMD,              // A sub-command array.
};

// A command definition.  Note that this is POD.
struct Command {
  const char *name;             // Name within this command array.
  const char *description;      // Help text (what the command does).
  const char *arguments;        // Usage text (arguments).
  CommandType type;             // Command type.
  int min_num_arguments;        // Minimum number of arguments.
  int max_num_arguments;        // Maximum number of args (-1 for no limit).
  union {                       // Depends on the type field.
    CommandFunction function;
    CommandVector *subcommands;
  };

  // Creates a new leaf Command.
  static Command CreateCommand(const char *name, const char *description,
                               const char *arguments, CommandType type,
                               int min_num_arguments, int max_num_arguments,
                               CommandFunction function) {
    CHECK_NOTNULL(name);
    CHECK_NOTNULL(description);
    CHECK_NOTNULL(arguments);
    CHECK(type == CMD_TYPE_GETTER || type == CMD_TYPE_SETTER ||
          type == CMD_TYPE_INIT);
    CHECK_NOTNULL(function);

    Command command;
    command.name = name;
    command.description = description;
    command.arguments = arguments;
    command.type = type;
    command.min_num_arguments = min_num_arguments;
    command.max_num_arguments = max_num_arguments;
    command.function = function;
    return command;
  }

  // Creates a new sub-menu Command.
  static Command CreateSubmenu(const char *name, const char *description,
                               const char *arguments,
                               ::std::initializer_list<Command> subcommands) {
    CHECK_NOTNULL(name);
    CHECK_NOTNULL(description);
    CHECK_NOTNULL(arguments);

    Command command;
    command.name = name;
    command.description = description;
    command.arguments = arguments;
    command.type = CMD_TYPE_SUBCMD;
    command.subcommands = new CommandVector(subcommands);
    return command;
  }
};

// The naming of these two functions is odd, but it makes the call-sites a
// lot easier when defining command trees.
inline Command CMD(const char *name, const char *description,
                   const char *arguments, CommandType type,
                   int min_num_arguments, int max_num_arguments,
                   CommandFunction function) {
  return Command::CreateCommand(name, description, arguments, type,
                                min_num_arguments, max_num_arguments, function);
}
inline Command SUB(const char *name, const char *description,
                   const char *arguments,
                   ::std::initializer_list<Command> subcommands) {
  return Command::CreateSubmenu(name, description, arguments, subcommands);
}

// Registers a top-level command tree.  This will make a copy of the
// Command structure.  See comments at the definition of struct Command
// for details.
void RegisterRootCommand(const Command &new_command);

// Factory for creating ContainerApi instances.
typedef ResultCallback< ::util::StatusOr<ContainerApi *>> ContainerApiFactory;

// Looks up a command and executes it, or prints help. Does not own
// lmctfy_factory which must be a repeatable callback.
::util::Status RunCommand(const ::std::vector<string> &args,
                          OutputMap::Style output_style,
                          ContainerApiFactory *lmctfy_factory,
                          FILE *out);

// Looks up and print usage help (if any) for the given command string or root
// command tree if no part of the command tree matches.
void FindPartialCommandAndPrintUsage(FILE *out,
                                     const ::std::vector<string> &args);

// Prints a simple usage message and command list.  Pass commands=NULL for
// the root command set.
void PrintUsage(FILE *out, const CommandVector *commands);

// Prints the command tree in the standard short format.
void PrintCommandTree(FILE *out, const CommandVector *commands);

// Prints the command tree in the standard long format.
void PrintCommandTreeLong(FILE *out, const CommandVector *commands);

// Internal APIs.
namespace internal {

// Gets a pointer to the global vector of root Commands.
const CommandVector *GetRootCommands();

// Clears the global vector of root Commands.
void ClearRootCommands();

// Prints help for a particular Command.
void PrintCommandHelp(FILE *out, const Command *command,
                      const string &command_path);

// Finds the named Command in the vector, or returns NULL.
const Command *FindCommand(const CommandVector *commands, const string &name);

}  // namespace internal

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CLI_COMMAND_H_
