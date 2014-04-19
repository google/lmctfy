#ifndef LMCTFY_INCLUDE_CLMCTFY_STATUS_H_
#define LMCTFY_INCLUDE_CLMCTFY_STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifdef __cplusplus
}
#endif // __cplusplus

struct status {
  int error_code;

  // Null-terminated string allocated on heap.
  // Needs to be free()'ed.
  char *message;
};

#endif // LMCTFY_INCLUDE_CLMCTFY_STATUS_H_
