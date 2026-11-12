cmake_minimum_required(VERSION 3.24)
project(BrowserControlSDK VERSION 0.2.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanBrowserControlSDK STATIC
  "sdk/SDK/Cpp/BrowserControl.cpp"
)
add_library(BrowserControlSDK::sdk ALIAS TitanBrowserControlSDK)
set_target_properties(TitanBrowserControlSDK PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanBrowserControlSDK PUBLIC cxx_std_17)
target_compile_definitions(TitanBrowserControlSDK PUBLIC )
target_compile_definitions(TitanBrowserControlSDK PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanBrowserControlSDK PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanBrowserControlSDK PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK/Cpp>"
  "$<INSTALL_INTERFACE:include/SDK/Cpp>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/ThirdParty/nlohmann>"
  "$<INSTALL_INTERFACE:include/ThirdParty/nlohmann>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/ThirdParty>"
  "$<INSTALL_INTERFACE:include/ThirdParty>"
)
target_link_libraries(TitanBrowserControlSDK PUBLIC advapi32 bcrypt comctl32 comdlg32 crypt32 dwmapi gdi32 normaliz ole32 oleaut32 shell32 shlwapi user32 version ws2_32)
install(TARGETS TitanBrowserControlSDK EXPORT BrowserControlSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/browser-control-sdk)
install(EXPORT BrowserControlSDKTargets NAMESPACE BrowserControlSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BrowserControlSDK)
configure_file("BrowserControlSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/BrowserControlSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/BrowserControlSDKConfigVersion.cmake" VERSION 0.2.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BrowserControlSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/BrowserControlSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BrowserControlSDK)
