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


const char KernelFiles::CPUAcct::kUsage[] = "cpuacct.usage";
const char KernelFiles::CPUAcct::kUsagePerCPU[] = "cpuacct.usage_percpu";
const char KernelFiles::CPUAcct::kHistogram[] = "cpuacct.histogram";

const char KernelFiles::Memory::kCompressionEnabled[] =
    "memory.compression_enabled";
const char KernelFiles::Memory::kCompressionSamplingRatio[] =
    "memory.compression_sampling_ratio";
const char KernelFiles::Memory::kCompressionSamplingStats[] =
    "memory.compression_sampling_stats";
const char KernelFiles::Memory::kCompressionThrashingStats[] =
    "memory.compression_thrashing_stats";
const char KernelFiles::Memory::kDirtyBackgroundLimitInBytes[] =
    "memory.dirty_background_limit_in_bytes";
const char KernelFiles::Memory::kDirtyLimitInBytes[] =
    "memory.dirty_limit_in_bytes";
const char KernelFiles::Memory::kIdlePageStats[] =
    "memory.idle_page_stats";
const char KernelFiles::Memory::kLimitInBytes[] = "memory.limit_in_bytes";
const char KernelFiles::Memory::kMaxLimitInBytes[] =
    "memory.max_limit_in_bytes";
const char KernelFiles::Memory::kMaxUsageInBytes[] =
    "memory.max_usage_in_bytes";
const char KernelFiles::Memory::kOomControl[] = "memory.oom_control";
const char KernelFiles::Memory::kOomScoreBadnessOverlimitBias[] =
    "memory.oom_score_badness_overlimit_bias";
const char KernelFiles::Memory::kOomScoreBadness[] =
    "memory.oom_score_badness";
const char KernelFiles::Memory::kShmId[] = "memory.charge_shmid";
const char KernelFiles::Memory::kSlabinfo[] = "memory.slabinfo";
const char KernelFiles::Memory::kSoftLimitInBytes[] =
    "memory.soft_limit_in_bytes";
const char KernelFiles::Memory::kStat[] = "memory.stat";
const char KernelFiles::Memory::kStepSizeInBytes[] =
    "memory.step_size_in_bytes";
const char KernelFiles::Memory::kSwapfile[] = "memory.swapfile";
const char KernelFiles::Memory::kUsageInBytes[] = "memory.usage_in_bytes";
const char KernelFiles::Memory::kForceEmpty[] = "memory.force_empty";
const char KernelFiles::Memory::kTryToFreePages[] = "memory.try_to_free_pages";

const char KernelFiles::Memory::IdlePageStats::kStale[] = "stale";
const char KernelFiles::Memory::Stat::kHierarchicalMemoryLimit[] =
    "hierarchical_memory_limit";

const char KernelFiles::CGroup::Children::kCount[] = "cgroup.children_count";
const char KernelFiles::CGroup::Children::kLimit[] = "cgroup.children_limit";
const char KernelFiles::CGroup::Children::kClone[] = "cgroup.clone_children";
const char KernelFiles::CGroup::kProcesses[] = "cgroup.procs";
const char KernelFiles::CGroup::kEventfdInterface[] = "cgroup.event_control";
const char KernelFiles::CGroup::kTasks[] = "tasks";

const char KernelFiles::kJobId[] = "job.id";
const char KernelFiles::kOOMDelay[] = "memory.oom_delay_millisecs";

}  // namespace lmctfy
}  // namespace containers
