/* SPDX-License-Identifier: MIT */
#ifndef BROWSER_CONTROL_SDK_CSDK_H
#define BROWSER_CONTROL_SDK_CSDK_H
#include <stdint.h>
#if !defined(_WIN32) || !defined(_WIN64)
#error This SDK requires Windows x64.
#endif
#ifdef __cplusplus
#define BROWSER_CONTROL_SDK_CSDK_NOEXCEPT noexcept
extern "C" {
#else
#define BROWSER_CONTROL_SDK_CSDK_NOEXCEPT
#endif
#if defined(BROWSER_CONTROL_SDK_CSDK_BUILD)
#define BROWSER_CONTROL_SDK_CSDK_API __declspec(dllexport)
#else
#define BROWSER_CONTROL_SDK_CSDK_API __declspec(dllimport)
#endif
/* open is inert. One active session per loaded product DLL.
   call accepts length-delimited UTF-8 JSON and returns an allocated UTF-8
   response, or NULL on allocation failure. Always free with this same DLL.
   Session/operation handles are scoped to the client that returned them. */
BROWSER_CONTROL_SDK_CSDK_API uint32_t browser_control_sdk_json_open(uint64_t *session) BROWSER_CONTROL_SDK_CSDK_NOEXCEPT;
BROWSER_CONTROL_SDK_CSDK_API uint32_t browser_control_sdk_json_close(uint64_t session) BROWSER_CONTROL_SDK_CSDK_NOEXCEPT;
BROWSER_CONTROL_SDK_CSDK_API char *browser_control_sdk_json_call(uint64_t session,const char *request,uint32_t length) BROWSER_CONTROL_SDK_CSDK_NOEXCEPT;
BROWSER_CONTROL_SDK_CSDK_API void browser_control_sdk_json_free(char *response) BROWSER_CONTROL_SDK_CSDK_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef BROWSER_CONTROL_SDK_CSDK_NOEXCEPT
#endif
