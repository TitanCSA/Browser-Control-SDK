vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/TitanCSA/Browser-Control-SDK/releases/download/sdk-v0.2.0/browser-control-sdk-sdk-0.2.0.tar.gz"
    FILENAME "browser-control-sdk-sdk-0.2.0.tar.gz"
    SHA512 273d6c6696092bf480897b788c03f995ca774654b6d326c5040da7a7cb99fabe18b5998c63e0e248cdf7cb581018ad85e8b348d7c6a5392978ffe2c168f25472
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME BrowserControlSDK CONFIG_PATH lib/cmake/BrowserControlSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(GLOB_RECURSE EXTRA_COPYRIGHT "${SOURCE_PATH}/licenses/*.txt")
foreach(NOTICE_FILE IN ITEMS NOTICE NOTICE.txt)
    if(EXISTS "${SOURCE_PATH}/${NOTICE_FILE}")
        list(APPEND EXTRA_COPYRIGHT "${SOURCE_PATH}/${NOTICE_FILE}")
    endif()
endforeach()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" ${EXTRA_COPYRIGHT})
