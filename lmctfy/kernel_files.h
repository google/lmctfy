// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef SRC_KERNEL_FILES_H__
#define SRC_KERNEL_FILES_H__

namespace containers {
namespace lmctfy {

struct KernelFiles {
 public:
  struct CPUSet {
   public:
    // List of CPUs
    static const char kCPUs[];

    // List of memory nodes
    static const char kMemNodes[];
  };

  struct Cpu {
    // Number of runnable processes.
    static const char kNumRunning[];

    // CPU throughput.
    static const char kShares[];

    // CPU latency parameter.
    static const char kLatency[];

    // Placement strategy to load balance across CPUs.
    static const char kPlacementStrategy[];

    // Enforcement period for throttling.
    static const char kHardcapPeriod[];

    // Quota allowed per throttling period.
    static const char kHardcapQuota[];

    // Stats about throttling activity.
    static const char kThrottlingStats[];
  };

  
  // Files related to CPU accounting
  struct CPUAcct {
   public:
    static const char kUsage[];
    static const char kUsagePerCPU[];
    static const char kHistogram[];
  };

  // File related to Memory subsystem.
  struct Memory {
   public:
    static const char kCompressionEnabled[];
    static const char kCompressionSamplingRatio[];
    static const char kCompressionSamplingStats[];
    static const char kCompressionThrashingStats[];
    static const char kDirtyBackgroundLimitInBytes[];
    static const char kDirtyLimitInBytes[];
    static const char kIdlePageStats[];
    static const char kLimitInBytes[];
    static const char kMaxLimitInBytes[];
    static const char kMaxUsageInBytes[];
    static const char kOomControl[];
    static const char kOomScoreBadness[];
    static const char kOomScoreBadnessOverlimitBias[];
    static const char kShmId[];
    static const char kSlabinfo[];
    static const char kSoftLimitInBytes[];
    static const char kStat[];
    static const char kStepSizeInBytes[];
    static const char kSwapfile[];
    static const char kUsageInBytes[];
    static const char kForceEmpty[];
    static const char kTryToFreePages[];

    // Fields in kIdlePageStats file.
    struct IdlePageStats {
     public:
      static const char kStale[];
    };

    // Fields in kStat file.
    struct Stat {
     public:
      static const char kHierarchicalMemoryLimit[];
    };
  };

  struct CGroup {
   public:
    struct Children {
     public:
      static const char kCount[];
      static const char kLimit[];
      static const char kClone[];
    };

    static const char kProcesses[];
    static const char kEventfdInterface[];
    static const char kTasks[];
  };

  // TODO(jonathanw): Describe (and organize?) these properly
  static const char kJobId[];
  static const char kOOMDelay[];
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_KERNEL_FILES_H__
