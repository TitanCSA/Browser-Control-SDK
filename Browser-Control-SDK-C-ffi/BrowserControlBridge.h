#pragma once
#include <stdint.h>
#ifdef _WIN32
#define BCS_API __declspec(dllexport)
#else
#define BCS_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* UTF-8 inputs. Returned UTF-8 strings belong to the caller: release with bcs_free.
   Null endpoint discovers a running instance. Handle zero indicates an error.
   bcs_call returns 0 for success, 1 for an error JSON {code,message,details}.
   Browser calls have a deadline and are never automatically retried. */
BCS_API uint64_t bcs_connect(const char* endpoint, char** error);
BCS_API int bcs_call(uint64_t handle, const char* method, const char* params_json, uint32_t timeout_ms, char** result);
BCS_API void bcs_close(uint64_t handle);
BCS_API void bcs_free(void* text);
#ifdef __cplusplus
}
#endif
