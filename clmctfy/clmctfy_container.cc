#include "clmctfy.h"
#include "lmctfy.h"
#include "clmctfy_internal.h"
#include "util/task/statusor.h"
#include <vector>

using namespace ::containers::lmctfy;
using ::util::internal::status_copy;
using ::util::internal::status_new;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;
using ::std::vector;

#define STATUS_OK UTIL__ERROR__CODE__OK

int lmctfy_container_exec(struct status *s,
                          struct container *container,
                          const int argc,
                          const char **argv) {
  int ret = STATUS_OK;
  CHECK_NOTNULL_OR_RETURN(s, container);
  CHECK_POSITIVE_OR_RETURN(s, argc);

  vector<string> cmds(argc);
  int i = 0;
  for (i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  Status status = container->container_->Exec(cmds);
  return status_copy(s, status);
}

void lmctfy_delete_container(struct container *container) {
  if (container != NULL) {
    if (container->container_ != NULL) {
      delete container->container_;
    }
    delete container;
  }
}
