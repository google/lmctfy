#ifndef LMCTFY_C_BINDING_H_
#define LMCTFY_C_BINDING_H_

#include "codes.pb-c.h"

extern "C" {

struct status;

int status_is_ok(struct status *);
int status_get_code(struct status *);
const char *status_get_message(struct status *);

}

#endif // LMCTFY_C_BINDING_H_
