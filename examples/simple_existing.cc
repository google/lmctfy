// Simple example of the ContaineApi usage for accessing information about an
// existing container.

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"

using ::containers::lmctfy::Container;
using ::containers::lmctfy::ContainerStats;
using ::containers::lmctfy::ContainerApi;
using ::std::cout;
using ::std::endl;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

int main() {
  // Get an instance of ContainerApi.
  unique_ptr<ContainerApi> lmctfy;
  {
    StatusOr<ContainerApi *> statusor = ContainerApi::New();
    if (!statusor.ok()) {
      cout << "Failed to instantiate ContainerApi: "
           << statusor.status().ToString() << endl;
      return 1;
    }
    lmctfy.reset(statusor.ValueOrDie());
  }

  // Detect the current container.
  {
    StatusOr<string> statusor = lmctfy->Detect();
    if (!statusor.ok()) {
      cout << "Failed to detect the current container: "
           << statusor.status().ToString() << endl;
      return 1;
    }
    cout << "Current container:" << statusor.ValueOrDie() << endl;
  }

  // Get the current container.
  unique_ptr<Container> container;
  {
    StatusOr<Container *> statusor = lmctfy->Get(".");
    if (!statusor.ok()) {
      cout << "Failed to get container: " << statusor.status().ToString()
           << endl;
      return 1;
    }
    container.reset(statusor.ValueOrDie());
  }

  // Get the memory usage of the current container.
  {
    StatusOr<ContainerStats> statusor =
        container->Stats(Container::STATS_SUMMARY);
    if (!statusor.ok()) {
      cout << "Failed to get container stats: " << statusor.status().ToString()
           << endl;
      return 1;
    }
    cout << "Memory usage: " << statusor.ValueOrDie().memory().usage() << endl
         << "Working set: " << statusor.ValueOrDie().memory().working_set()
         << endl;
  }

  return 0;
}
