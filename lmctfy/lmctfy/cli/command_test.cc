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

// Tests for command.cc

#include "lmctfy/cli/command.h"

#include <stdio.h>
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "util/testing/pipe_file.h"
#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

using ::util_testing::PipeFile;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::ContainsRegex;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {

// Some generic command functions, used in several tests
static Status CommandFunc1(const vector<string> &argv, const ContainerApi *lmctfy,
                           OutputMap *output) {
  return Status::OK;
}
static int cmd_func_magic;
static Status cmd_func_retval;
static Status CommandFunc2(const vector<string> &argv, const ContainerApi *lmctfy,
                           OutputMap *output) {
  cmd_func_magic = 76;
  return cmd_func_retval;
}

class CommandTest : public ::testing::Test {
 protected:
  virtual void TearDown() {
    internal::ClearRootCommands();
  }
};


using internal::GetRootCommands;

TEST_F(CommandTest, RootCommandsEmptyAtStart) {
  ASSERT_EQ(0, GetRootCommands()->size());
}

TEST_F(CommandTest, RegisterRootCommandSingle) {
  Command cmd;
  cmd.name = "foo";
  cmd.type = CMD_TYPE_GETTER;
  cmd.min_num_arguments = 0;
  cmd.max_num_arguments = 0;
  cmd.function = NULL;
  RegisterRootCommand(cmd);
  ASSERT_EQ(1, GetRootCommands()->size());
  EXPECT_STREQ(GetRootCommands()->at(0).name, "foo");
}

TEST_F(CommandTest, RegisterRootCommandMultiple) {
  Command cmd;
  cmd.name = "foo";
  cmd.type = CMD_TYPE_GETTER;
  cmd.min_num_arguments = 0;
  cmd.max_num_arguments = 0;
  cmd.function = NULL;
  RegisterRootCommand(cmd);
  cmd.name = "bar";
  RegisterRootCommand(cmd);
  ASSERT_EQ(2, GetRootCommands()->size());
  EXPECT_STREQ(GetRootCommands()->at(0).name, "bar");
  EXPECT_STREQ(GetRootCommands()->at(1).name, "foo");
}

TEST_F(CommandTest, CreateCommand) {
  ASSERT_EQ(0, GetRootCommands()->size());

  Command cmd = Command::CreateCommand("n", "d", "a", CMD_TYPE_GETTER, 0, 0,
                                       CommandFunc1);
  RegisterRootCommand(cmd);

  ASSERT_EQ(1, GetRootCommands()->size());
  EXPECT_STREQ(GetRootCommands()->at(0).name, "n");
  EXPECT_STREQ(GetRootCommands()->at(0).description, "d");
  EXPECT_STREQ(GetRootCommands()->at(0).arguments, "a");
  EXPECT_EQ(CMD_TYPE_GETTER, GetRootCommands()->at(0).type);
  EXPECT_EQ(GetRootCommands()->at(0).min_num_arguments, 0);
  EXPECT_EQ(GetRootCommands()->at(0).max_num_arguments, 0);
  EXPECT_TRUE(GetRootCommands()->at(0).function == CommandFunc1);
}

TEST_F(CommandTest, CreateSubmenu) {
  ASSERT_EQ(0, GetRootCommands()->size());

  Command cmd = Command::CreateSubmenu("n", "d", "a", {});
  RegisterRootCommand(cmd);

  ASSERT_EQ(1, GetRootCommands()->size());
  EXPECT_STREQ(GetRootCommands()->at(0).name, "n");
  EXPECT_STREQ(GetRootCommands()->at(0).description, "d");
  EXPECT_STREQ(GetRootCommands()->at(0).arguments, "a");
}

TEST_F(CommandTest, PrintCommandList) {
  FILE *out = fopen("/dev/null", "rw");
  ASSERT_TRUE(out != NULL);
  CommandVector cmdvec = {CMD("a", "", "", CMD_TYPE_GETTER, 0, 0,
                              CommandFunc1)};
  // Just make sure it doesn't blow up.
  // TODO(thockin): these should use a PipeFile and check the output.
  PrintUsage(out, &cmdvec);
  PrintUsage(out, NULL);
  fclose(out);
}

TEST_F(CommandTest, PrintCommandHelp) {
  FILE *out = fopen("/dev/null", "rw");
  ASSERT_TRUE(out != NULL);
  Command cmd = Command::CreateCommand("n", "d", "a", CMD_TYPE_GETTER, 0, 0,
                                       CommandFunc1);
  // Just make sure it doesn't blow up.
  // TODO(thockin): this should use a PipeFile and check the output.
  internal::PrintCommandHelp(out, &cmd, "cmdpath");
  fclose(out);
}

TEST_F(CommandTest, PrintCommandTree) {
  FILE *out = fopen("/dev/null", "rw");
  ASSERT_TRUE(out != NULL);

  // Set up a fake command tree.
  ASSERT_EQ(0, GetRootCommands()->size());
  Command cmd = Command::CreateCommand("n1", "d1", "a1", CMD_TYPE_GETTER, 0, 0,
                                       CommandFunc1);
  RegisterRootCommand(cmd);
  Command sub = Command::CreateSubmenu("n2", "d2", "a2", {
      Command::CreateCommand("n2.1", "d2.1", "a2.1", CMD_TYPE_GETTER, 0, 0,
                             CommandFunc1),
      Command::CreateCommand("n2.2", "d2.2", "a2.2", CMD_TYPE_SETTER, 0, 0,
                             CommandFunc1),
      Command::CreateSubmenu("n2.3", "d2.3", "a2.3", {})
  });
  RegisterRootCommand(sub);

  // Just make sure it doesn't blow up.
  // TODO(thockin): these should use a PipeFile and check the output.
  PrintCommandTree(out, NULL);
  PrintCommandTreeLong(out, NULL);
  fclose(out);
}

TEST_F(CommandTest, FindCommand) {
  CommandVector cmdvec = {
    Command::CreateCommand("n1", "d", "a", CMD_TYPE_GETTER, 0, 0, CommandFunc1),
    Command::CreateCommand("n2", "d", "a", CMD_TYPE_GETTER, 0, 0, CommandFunc1),
    Command::CreateCommand("n3", "d", "a", CMD_TYPE_GETTER, 0, 0, CommandFunc1)
  };

  EXPECT_STREQ("n1", internal::FindCommand(&cmdvec, "n1")->name);
  EXPECT_STREQ("n2", internal::FindCommand(&cmdvec, "n2")->name);
  EXPECT_STREQ("n3", internal::FindCommand(&cmdvec, "n3")->name);
  EXPECT_TRUE(internal::FindCommand(&cmdvec, "n4") == NULL);
}

class SampleTreeCommandTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    Command root1 = SUB("r1", "End of alphabet commands", "", {
        CMD("c1", "This does x", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc1),
        CMD("c2", "This does y", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc1),
        CMD("c3", "This does z", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc1),
    });
    Command root2 = SUB("r2", "Beginning of alphabet commands", "", {
      CMD("c1", "This does a", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc1),
      CMD("c2", "This does b", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc2),
      CMD("c3", "This does c", "", CMD_TYPE_GETTER, 0, 0, &CommandFunc1),
    });
    RegisterRootCommand(root1);
    RegisterRootCommand(root2);
  }
  virtual void TearDown() {
    internal::ClearRootCommands();
  }

  unique_ptr<CommandVector> root1_sub_;
  unique_ptr<CommandVector> root2_sub_;
};

StatusOr<ContainerApi *> MockFactory() {
  return new StrictMockContainerApi();
}

TEST_F(SampleTreeCommandTest, RunCommand) {
  Status ret;
  vector<string> args = {"test", "r2", "c2"};
  unique_ptr<ContainerApiFactory> mock_factory(NewPermanentCallback(&MockFactory));

  // Run some commands and verify.

  cmd_func_retval = Status::OK;
  cmd_func_magic = 0;
  ret = RunCommand(args, OutputMap::STYLE_VALUES, mock_factory.get(), stdout);
  EXPECT_TRUE(ret.ok());
  EXPECT_EQ(76, cmd_func_magic);

  cmd_func_retval = Status::CANCELLED;
  cmd_func_magic = 0;
  ret = RunCommand(args, OutputMap::STYLE_VALUES, mock_factory.get(), stdout);
  EXPECT_EQ(Status::CANCELLED, ret);
  EXPECT_EQ(76, cmd_func_magic);

  // Run a bad command.
  vector<string> bad_args = {"test", "r2", "c8"};

  cmd_func_retval = Status::OK;
  cmd_func_magic = 0;
  ret = RunCommand(bad_args, OutputMap::STYLE_VALUES, mock_factory.get(),
                   stdout);
  EXPECT_FALSE(ret.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, ret.error_code());

  // Run some help commands.
  vector<string> help_args = {"test", "r2", "c2", "help"};

  cmd_func_retval = Status::CANCELLED;
  cmd_func_magic = 0;
  ret = RunCommand(help_args, OutputMap::STYLE_VALUES, mock_factory.get(),
                   stdout);
  EXPECT_TRUE(ret.ok());
  EXPECT_EQ(0, cmd_func_magic);

  help_args.push_back("extra");

  cmd_func_retval = Status::CANCELLED;
  cmd_func_magic = 0;
  ret = RunCommand(help_args, OutputMap::STYLE_VALUES, mock_factory.get(),
                   stdout);
  EXPECT_TRUE(ret.ok());
  EXPECT_EQ(0, cmd_func_magic);
}

TEST_F(SampleTreeCommandTest, FindPartialCommandAndPrintUsageNormal) {
  // When a valid partial command is passed to FindPartialCommandAndPrintUsage,
  // it should print out the correct usage information for the indicated
  // subtree.

  PipeFile out;
  out.Open();
  FILE *out_write = out.GetWriteFile();

  vector<string> args = {"test", "r1"};

  FindPartialCommandAndPrintUsage(out_write, args);
  string out_str = out.GetContents();

  // Make sure a few key substrings appear in the output
  EXPECT_THAT(out_str, ContainsRegex("Usage"));
  EXPECT_THAT(out_str, ContainsRegex("c1"));
  EXPECT_THAT(out_str, ContainsRegex("c2"));
  EXPECT_THAT(out_str, ContainsRegex("c3"));
  EXPECT_THAT(out_str, ContainsRegex("This does x"));
  EXPECT_THAT(out_str, ContainsRegex("This does y"));
  EXPECT_THAT(out_str, ContainsRegex("This does z"));
}

TEST_F(SampleTreeCommandTest, FindPartialCommandAndPrintUsageNoSubCommand) {
  // When the command passed to FindPartialCommandAndPrintUsage is a leaf of
  // the command tree, it should log a fatal error and die.
  vector<string> args = {"test", "r1", "c2"};

  FILE *null_file = fopen("/dev/null", "w");
  EXPECT_DEATH(FindPartialCommandAndPrintUsage(null_file, args),
               "this should only be called when a command is NOT found");
}

TEST_F(SampleTreeCommandTest,
       FindPartialCommandAndPrintUsageNonExistentCommand) {
  // When the command passed to FindPartialCommandAndPrintUsage does not exist,
  // it should print out usage information for first level commands in the tree.
  PipeFile out;
  out.Open();
  FILE *out_write = out.GetWriteFile();
  vector<string> args = {"test", "badcommand", "badsubcommand"};

  FindPartialCommandAndPrintUsage(out_write, args);
  string out_str = out.GetContents();

  // Make sure a few key substrings appear in the output
  EXPECT_THAT(out_str, ContainsRegex("Usage"));
  EXPECT_THAT(out_str, ContainsRegex("r1"));
  EXPECT_THAT(out_str, ContainsRegex("r2"));
  EXPECT_THAT(out_str, ContainsRegex("End of alphabet commands"));
  EXPECT_THAT(out_str, ContainsRegex("Beginning of alphabet commands"));
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
