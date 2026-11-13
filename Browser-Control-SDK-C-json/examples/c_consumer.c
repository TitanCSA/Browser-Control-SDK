#include "BrowserControlSDKJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(browser_control_sdk_json_open(&session))return 1;char *reply=browser_control_sdk_json_call(session,"{",1);if(!reply){browser_control_sdk_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;browser_control_sdk_json_free(reply);if(browser_control_sdk_json_close(session))return 3;return failed;}
