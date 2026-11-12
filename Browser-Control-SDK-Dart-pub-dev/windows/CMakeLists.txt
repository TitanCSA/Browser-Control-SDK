cmake_minimum_required(VERSION 3.14)
project(browser_control_sdk LANGUAGES CXX)
set(browser_control_sdk_bundled_libraries
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib/native/BrowserControlBridge.dll"
  PARENT_SCOPE)
