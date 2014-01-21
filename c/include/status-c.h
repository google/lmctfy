#ifndef LMCTFY_C_BINDING_H_
#define LMCTFY_C_BINDING_H_

#include "codes.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct status;

int status_is_ok(struct status *);
int status_get_code(struct status *);
const char *status_get_message(struct status *);
void status_release(struct status *);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LMCTFY_C_BINDING_H_
