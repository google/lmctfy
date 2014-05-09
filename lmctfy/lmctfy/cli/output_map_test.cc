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

// Tests for output_map.cc

#include "lmctfy/cli/output_map.h"

#include "base/logging.h"
#include "util/testing/pipe_file.h"
#include "gtest/gtest.h"

using util_testing::PipeFile;

namespace containers {
namespace lmctfy {
namespace cli {

class OutputMapTest : public ::testing::Test {
};


TEST_F(OutputMapTest, Ctors) {
  OutputMap output_map1;
  ASSERT_EQ(0, output_map1.NumPairs());

  OutputMap output_map2("k", "v");
  ASSERT_EQ(1, output_map2.NumPairs());
  EXPECT_EQ("k", output_map2.GetKey(0));
  EXPECT_EQ("v", output_map2.GetValue(0));
}

TEST_F(OutputMapTest, Add) {
  OutputMap output_map;

  // Add a pair.
  output_map.Add("k0", "v0");
  ASSERT_EQ(1, output_map.NumPairs());
  EXPECT_EQ("k0", output_map.GetKey(0));
  EXPECT_EQ("v0", output_map.GetValue(0));

  // Add another pair.
  output_map.Add("k1", "v1");
  ASSERT_EQ(2, output_map.NumPairs());
  EXPECT_EQ("k0", output_map.GetKey(0));
  EXPECT_EQ("v0", output_map.GetValue(0));
  EXPECT_EQ("k1", output_map.GetKey(1));
  EXPECT_EQ("v1", output_map.GetValue(1));

  // Add a few more pairs.
  output_map.AddBool("k2", true).Add("k3", "v3");
  ASSERT_EQ(4, output_map.NumPairs());
  EXPECT_EQ("k0", output_map.GetKey(0));
  EXPECT_EQ("v0", output_map.GetValue(0));
  EXPECT_EQ("k1", output_map.GetKey(1));
  EXPECT_EQ("v1", output_map.GetValue(1));
  EXPECT_EQ("k2", output_map.GetKey(2));
  EXPECT_EQ("yes", output_map.GetValue(2));
  EXPECT_EQ("k3", output_map.GetKey(3));
  EXPECT_EQ("v3", output_map.GetValue(3));

  // Add a duplicate key.
  output_map.Add("k1", "v4");
  ASSERT_EQ(5, output_map.NumPairs());
  EXPECT_EQ("k0", output_map.GetKey(0));
  EXPECT_EQ("v0", output_map.GetValue(0));
  EXPECT_EQ("k1", output_map.GetKey(1));
  EXPECT_EQ("v1", output_map.GetValue(1));
  EXPECT_EQ("k2", output_map.GetKey(2));
  EXPECT_EQ("yes", output_map.GetValue(2));
  EXPECT_EQ("k3", output_map.GetKey(3));
  EXPECT_EQ("v3", output_map.GetValue(3));

  // Test the key regex.
  OutputMap output_mapregex1;
  EXPECT_DEATH(output_mapregex1.Add("-key", "value"), "");
  EXPECT_DEATH(output_mapregex1.Add(".key", "value"), "");
  EXPECT_DEATH(output_mapregex1.Add(".key", "value"), "");

  OutputMap output_mapregex2;
  EXPECT_EQ(0, output_mapregex2.NumPairs());
  output_mapregex2.Add("a-key", "value");
  EXPECT_EQ(1, output_mapregex2.NumPairs());
  output_mapregex2.Add("a.key", "value");
  EXPECT_EQ(2, output_mapregex2.NumPairs());
  output_mapregex2.Add("3.1415", "value");
  EXPECT_EQ(3, output_mapregex2.NumPairs());
  output_mapregex2.Add("A_KEY.name-93", "value");
  EXPECT_EQ(4, output_mapregex2.NumPairs());
}

TEST_F(OutputMapTest, PrintValues) {
  OutputMap output_map;
  PipeFile pf;
  ASSERT_TRUE(pf.Open());

  output_map.Add("k0", "v0");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_VALUES);
  EXPECT_EQ("v0\n", pf.GetContents());

  output_map.Add("k1", "v1");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_VALUES);
  EXPECT_EQ("v0\nv1\n", pf.GetContents());

  output_map.Add("k2", "v2");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_VALUES);
  EXPECT_EQ("v0\nv1\nv2\n", pf.GetContents());
}

TEST_F(OutputMapTest, PrintLong) {
  OutputMap output_map;
  PipeFile pf;
  ASSERT_TRUE(pf.Open());

  output_map.Add("k0", "v0");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_LONG);
  EXPECT_EQ("k0                   | v0\n\n", pf.GetContents());

  output_map.Add("k1", "v1");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_LONG);
  EXPECT_EQ("k0                   | v0\n\n"
            "k1                   | v1\n\n", pf.GetContents());

  output_map.Add("k2", "v2");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_LONG);
  EXPECT_EQ("k0                   | v0\n\n"
            "k1                   | v1\n\n"
            "k2                   | v2\n\n", pf.GetContents());
}

TEST_F(OutputMapTest, PrintPairs) {
  OutputMap output_map;
  PipeFile pf;
  ASSERT_TRUE(pf.Open());

  output_map.Add("k0", "v0");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_PAIRS);
  EXPECT_EQ("k0=\"v0\"\n", pf.GetContents());

  output_map.Add("k1", "v1");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_PAIRS);
  EXPECT_EQ("k0=\"v0\"\nk1=\"v1\"\n", pf.GetContents());

  output_map.Add("k2", "v2");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_PAIRS);
  EXPECT_EQ("k0=\"v0\"\nk1=\"v1\"\nk2=\"v2\"\n", pf.GetContents());

  output_map.Add("k3", "v\"3\"");
  output_map.Print(pf.GetWriteFile(), OutputMap::STYLE_PAIRS);
  EXPECT_EQ("k0=\"v0\"\nk1=\"v1\"\nk2=\"v2\"\nk3=\"v\\\"3\\\"\"\n",
            pf.GetContents());
}

TEST_F(OutputMapTest, PrintNothing) {
  OutputMap output_map;
  PipeFile pf;
  ASSERT_TRUE(pf.Open());

  output_map.Add("k0", "v0");
  output_map.Print(pf.GetWriteFile(), static_cast<OutputMap::Style>(-1));
  EXPECT_EQ("", pf.GetContents());
}

}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
