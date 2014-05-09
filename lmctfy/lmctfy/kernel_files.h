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

  // Files related to blkio cgroup.
  struct BlockIO {
   public:
    // Per cgroup weight. Default weight of the group for all devices unless
    // overridden by per-device rule. Range 10 to 1000.
    static const char kWeight[];
    // Per device weight. Overrides the default weight above.
    // Setting to zero removes the weight.
    static const char kPerDeviceWeight[];
    // Disk time allocated to cgroup per device in milliseconds.
    static const char kDiskTime[];
    // Number of sectors transferred to/from the disk.
    static const char kSectors[];
    // Number of bytes serviced.
    static const char kServiceBytes[];
    // Number of IOs serviced.
    static const char kServiced[];
    // Time spent servicing I/Os.
    static const char kServiceTime[];
    // Time spent waiting for I/O access.
    static const char kWaitTime[];
    // Number of BIOS requests merged into requests for I/O ops.
    static const char kMerged[];
    // Number of requests queued.
    static const char kQueued[];
    // Historical average queue size.
    static const char kAvgQueueSize[];
    // Amount of time a container had to wait before servicing a request after
    // an IO request was added to an empty queue. Measured in nanoseconds.
    static const char kGroupWaitTime[];
    // Time spent without any pending requests.
    static const char kEmptyTime[];
    // Time spent idling waiting for better requests.
    static const char kIdleTime[];
    // Number of times a group was dequeued from a service tree for a device.
    static const char kDequeue[];
    // Hierarchical versions of the corresponding container interfaces.
    // Reports stats for the whole subtree.
    static const char kDiskTimeRecursive[];
    static const char kSectorsRecursive[];
    static const char kServiceBytesRecursive[];
    static const char kServicedRecursive[];
    static const char kServiceTimeRecursive[];
    static const char kMergedRecursive[];
    static const char kQueuedRecursive[];
    static const char kWaitTimeRecursive[];
    // Resets all existing stats for the container.
    static const char kResetStats[];
    // Maximum allowed read bytes per second.
    static const char kMaxReadBytesPerSecond[];
    // Maximum allowed write bytes per second.
    static const char kMaxWriteBytesPerSecond[];
    // Maximum allowed read IO operations per second.
    static const char kMaxReadIoPerSecond[];
    // Maximum allowed write IO operations per second.
    static const char kMaxWriteIoPerSecond[];
    // Number of IOs serviced at the throttling layer.
    static const char kThrottledIoServiced[];
    // Number of IO bytes serviced at the throttling layer.
    static const char kThrottledIoServiceBytes[];
  };


  // Files related to CPU accounting
  struct CPUAcct {
   public:
    static const char kUsage[];
    static const char kUsagePerCPU[];
    static const char kHistogram[];
    static const char kStat[];
  };

  // Files related to Memory subsystem.
  struct Memory {
   public:
    struct Memsw{
     public:
      static const char kLimitInBytes[];
      static const char kMaxUsageInBytes[];
      static const char kUsageInBytes[];
    };
    static const char kCompressionEnabled[];
    static const char kCompressionSamplingRatio[];
    static const char kCompressionSamplingStats[];
    static const char kCompressionThrashingStats[];
    static const char kDirtyRatio[];
    static const char kDirtyBackgroundRatio[];
    static const char kDirtyBackgroundLimitInBytes[];
    static const char kDirtyLimitInBytes[];
    static const char kFailCount[];
    static const char kIdlePageStats[];
    static const char kLimitInBytes[];
    static const char kMaxUsageInBytes[];
    static const char kNumaStat[];
    static const char kOomControl[];
    static const char kOomScoreBadness[];
    static const char kShmId[];
    static const char kSlabinfo[];
    static const char kSoftLimitInBytes[];
    static const char kStalePageAge[];
    static const char kStat[];
    static const char kSwapfile[];
    static const char kSwapSizePercent[];
    static const char kUsageInBytes[];
    static const char kForceEmpty[];
    static const char kTryToFreePages[];
    static const char kVmscanStat[];
    static const char kKMemChargeUsage[];

    // Fields in kIdlePageStats file.
    struct IdlePageStats {
     public:
      static const char kStale[];
    };

    // Fields in kStat file.
    struct Stat {
     public:
      static const char kHierarchicalMemoryLimit[];
      static const char kTotalInactiveAnon[];
      static const char kTotalInactiveFile[];
    };
  };

  // Files related to the Rlimit subsystem.
  struct RLimit {
    static const char kFdFailCount[];
    static const char kFdLimit[];
    static const char kFdMaxUsage[];
    static const char kFdUsage[];
  };

  struct Freezer {
    static const char kFreezerState[];
    static const char kFreezerParentFreezing[];
  };

  struct Device {
    // Devices to be allowed inside a container.
    static const char kDevicesAllow[];
    // Devices to be denied inside a container.
    static const char kDevicesDeny[];
    // List current device restrictions on a container.
    static const char kDevicesList[];
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
    static const char kTracingEnabled[];
  };

  // TODO(jonathanw): Describe (and organize?) these properly
  static const char kJobId[];
  static const char kOOMDelay[];
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_KERNEL_FILES_H__
