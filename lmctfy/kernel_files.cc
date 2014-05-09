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

#include "lmctfy/kernel_files.h"

namespace containers {
namespace lmctfy {

const char KernelFiles::CPUSet::kCPUs[] = "cpuset.cpus";
const char KernelFiles::CPUSet::kMemNodes[] = "cpuset.mems";

const char KernelFiles::Cpu::kNumRunning[] = "cpu.nr_running";
const char KernelFiles::Cpu::kShares[] = "cpu.shares";
const char KernelFiles::Cpu::kLatency[] = "cpu.lat";
const char KernelFiles::Cpu::kPlacementStrategy[] = "cpu.placement_strategy";
const char KernelFiles::Cpu::kHardcapPeriod[] = "cpu.cfs_period_us";
const char KernelFiles::Cpu::kHardcapQuota[] = "cpu.cfs_quota_us";
const char KernelFiles::Cpu::kThrottlingStats[] = "cpu.stat";

const char KernelFiles::BlockIO::kDiskTime[] = "blkio.time";
const char KernelFiles::BlockIO::kWeight[] = "blkio.weight";
const char KernelFiles::BlockIO::kPerDeviceWeight[] = "blkio.weight_device";
const char KernelFiles::BlockIO::kSectors[] = "blkio.sectors";
const char KernelFiles::BlockIO::kServiceBytes[] = "blkio.io_service_bytes";
const char KernelFiles::BlockIO::kServiceTime[] = "blkio.io_service_time";
const char KernelFiles::BlockIO::kServiced[] = "blkio.io_serviced";
const char KernelFiles::BlockIO::kWaitTime[] = "blkio.io_wait_time";
const char KernelFiles::BlockIO::kMerged[] = "blkio.io_merged";
const char KernelFiles::BlockIO::kQueued[] = "blkio.io_queued";
const char KernelFiles::BlockIO::kAvgQueueSize[] = "blkio.avg_queue_size";
const char KernelFiles::BlockIO::kGroupWaitTime[] = "blkio.group_wait_time";
const char KernelFiles::BlockIO::kEmptyTime[] = "blkio.empty_time";
const char KernelFiles::BlockIO::kIdleTime[] = "blkio.idle_time";
const char KernelFiles::BlockIO::kDequeue[] = "blkio.dequeue";
const char KernelFiles::BlockIO::kDiskTimeRecursive[] =
    "blkio.time_recursive";
const char KernelFiles::BlockIO::kSectorsRecursive[] =
    "blkio.sectors_recursive";
const char KernelFiles::BlockIO::kServiceBytesRecursive[] =
    "blkio.io_service_bytes_recursive";
const char KernelFiles::BlockIO::kServicedRecursive[] =
    "blkio.io_serviced_recursive";
const char KernelFiles::BlockIO::kServiceTimeRecursive[] =
    "blkio.io_service_time_recursive";
const char KernelFiles::BlockIO::kWaitTimeRecursive[] =
    "blkio.io_wait_time_recursive";
const char KernelFiles::BlockIO::kMergedRecursive[] =
    "blkio.io_merged_recursive";
const char KernelFiles::BlockIO::kQueuedRecursive[] =
    "blkio.io_queued_recursive";
const char KernelFiles::BlockIO::kResetStats[] = "blkio.reset_stats";
const char KernelFiles::BlockIO::kMaxReadBytesPerSecond[] =
    "blkio.throttle.read_bps_device";
const char KernelFiles::BlockIO::kMaxWriteBytesPerSecond[] =
    "blkio.throttle.write_bps_device";
const char KernelFiles::BlockIO::kMaxReadIoPerSecond[] =
    "blkio.throttle.read_iops_device";
const char KernelFiles::BlockIO::kMaxWriteIoPerSecond[] =
    "blkio.throttle.write_iops_device";
const char KernelFiles::BlockIO::kThrottledIoServiced[] =
    "blkio.throttle.io_serviced";
const char KernelFiles::BlockIO::kThrottledIoServiceBytes[] =
    "blkio.throttle.io_service_bytes";


const char KernelFiles::CPUAcct::kUsage[] = "cpuacct.usage";
const char KernelFiles::CPUAcct::kUsagePerCPU[] = "cpuacct.usage_percpu";
const char KernelFiles::CPUAcct::kHistogram[] = "cpuacct.histogram";
const char KernelFiles::CPUAcct::kStat[] = "cpuacct.stat";


const char KernelFiles::Memory::Memsw::kLimitInBytes[] =
    "memory.memsw.limit_in_bytes";
const char KernelFiles::Memory::Memsw::kMaxUsageInBytes[] =
    "memory.memsw.max_usage_in_bytes";
const char KernelFiles::Memory::Memsw::kUsageInBytes[] =
    "memory.memsw.usage_in_bytes";
const char KernelFiles::Memory::kCompressionEnabled[] =
    "memory.compression_enabled";
const char KernelFiles::Memory::kCompressionSamplingRatio[] =
    "memory.compression_sampling_ratio";
const char KernelFiles::Memory::kCompressionSamplingStats[] =
    "memory.compression_sampling_stats";
const char KernelFiles::Memory::kCompressionThrashingStats[] =
    "memory.compression_thrashing_stats";
const char KernelFiles::Memory::kDirtyRatio[] = "memory.dirty_ratio";
const char KernelFiles::Memory::kDirtyBackgroundRatio[] =
    "memory.dirty_background_ratio";
const char KernelFiles::Memory::kDirtyBackgroundLimitInBytes[] =
    "memory.dirty_background_limit_in_bytes";
const char KernelFiles::Memory::kDirtyLimitInBytes[] =
    "memory.dirty_limit_in_bytes";
const char KernelFiles::Memory::kFailCount[] =
    "memory.failcnt";
const char KernelFiles::Memory::kIdlePageStats[] =
    "memory.idle_page_stats";
const char KernelFiles::Memory::kLimitInBytes[] = "memory.limit_in_bytes";
const char KernelFiles::Memory::kMaxUsageInBytes[] =
    "memory.max_usage_in_bytes";
const char KernelFiles::Memory::kNumaStat[] = "memory.numa_stat";
const char KernelFiles::Memory::kOomControl[] = "memory.oom_control";
const char KernelFiles::Memory::kOomScoreBadness[] =
    "memory.oom_score_badness";
const char KernelFiles::Memory::kShmId[] = "memory.charge_shmid";
const char KernelFiles::Memory::kSlabinfo[] = "memory.slabinfo";
const char KernelFiles::Memory::kSoftLimitInBytes[] =
    "memory.soft_limit_in_bytes";
const char KernelFiles::Memory::kStalePageAge[] = "memory.stale_page_age";
const char KernelFiles::Memory::kStat[] = "memory.stat";
const char KernelFiles::Memory::kSwapfile[] = "memory.swapfile";
const char KernelFiles::Memory::kSwapSizePercent[] = "memory.swap_size_percent";
const char KernelFiles::Memory::kUsageInBytes[] = "memory.usage_in_bytes";
const char KernelFiles::Memory::kForceEmpty[] = "memory.force_empty";
const char KernelFiles::Memory::kTryToFreePages[] = "memory.try_to_free_pages";
const char KernelFiles::Memory::kVmscanStat[] = "memory.vmscan_stat";
const char KernelFiles::Memory::kKMemChargeUsage[] = "memory.kmem_charge_usage";

const char KernelFiles::Memory::IdlePageStats::kStale[] = "stale";
const char KernelFiles::Memory::Stat::kHierarchicalMemoryLimit[] =
    "hierarchical_memory_limit";
const char KernelFiles::Memory::Stat::kTotalInactiveAnon[] =
    "total_inactive_anon";
const char KernelFiles::Memory::Stat::kTotalInactiveFile[] =
    "total_inactive_file";

const char KernelFiles::RLimit::kFdFailCount[] = "rlimit.fd_failcnt";
const char KernelFiles::RLimit::kFdLimit[] = "rlimit.fd_limit";
const char KernelFiles::RLimit::kFdMaxUsage[] = "rlimit.fd_maxusage";
const char KernelFiles::RLimit::kFdUsage[] = "rlimit.fd_usage";

const char KernelFiles::Freezer::kFreezerState[] = "freezer.state";
const char KernelFiles::Freezer::kFreezerParentFreezing[] =
    "freezer.parent_freezing";

const char KernelFiles::Device::kDevicesAllow[] = "devices.allow";
const char KernelFiles::Device::kDevicesDeny[] = "devices.deny";
const char KernelFiles::Device::kDevicesList[] = "devices.list";

const char KernelFiles::CGroup::Children::kCount[] = "cgroup.children_count";
const char KernelFiles::CGroup::Children::kLimit[] = "cgroup.children_limit";
const char KernelFiles::CGroup::Children::kClone[] = "cgroup.clone_children";
const char KernelFiles::CGroup::kProcesses[] = "cgroup.procs";
const char KernelFiles::CGroup::kEventfdInterface[] = "cgroup.event_control";
const char KernelFiles::CGroup::kTasks[] = "tasks";
const char KernelFiles::CGroup::kTracingEnabled[] = "tracing_enabled";

const char KernelFiles::kJobId[] = "job.id";
const char KernelFiles::kOOMDelay[] = "memory.oom_delay_millisecs";

}  // namespace lmctfy
}  // namespace containers
