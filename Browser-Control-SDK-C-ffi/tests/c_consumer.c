#include "BrowserControlBridge.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    char* response = NULL;
    uint64_t handle = bcs_connect("__browser_sdk_missing_endpoint_7a015953__.json", &response);
    if (handle || !response || !strstr(response, "invalid_endpoint")) return 1;
    bcs_free(response); response = NULL;
    if (bcs_call(UINT64_MAX, "browser.info", "{}", 100, &response) != 1) return 2;
    if (!response || !strstr(response, "sdk_error")) return 3;
    bcs_free(response);
    bcs_close(UINT64_MAX);
    bcs_free(NULL);
    puts("C consumer: import library, invalid endpoint, invalid handle and ownership passed");
    return 0;
}
