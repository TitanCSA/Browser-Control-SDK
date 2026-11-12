from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, cmake_layout
class NativeSDK(ConanFile):
    name = "browser-control-sdk-sdk"
    version = "0.2.0"
    license = "MIT"
    description = "Public Windows x64 user-mode SDK for Browser-Control-SDK; driver not included."
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "CMakeLists.txt", "*.cmake.in", "sdk/*", "LICENSE", "licenses/*", "NOTICE*", "README.txt"
    generators = "CMakeToolchain", "CMakeDeps"
    def validate(self):
        if str(self.settings.os) != "Windows" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("Windows x64 is required")
    def layout(self):
        cmake_layout(self)
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def package(self):
        CMake(self).install()
    def package_info(self):
        self.cpp_info.libs = ["TitanBrowserControlSDK"]
        self.cpp_info.includedirs = ['include', 'include/SDK/Cpp', 'include/ThirdParty/nlohmann', 'include/ThirdParty']
        self.cpp_info.system_libs = ['advapi32', 'bcrypt', 'comctl32', 'comdlg32', 'crypt32', 'dwmapi', 'gdi32', 'normaliz', 'ole32', 'oleaut32', 'shell32', 'shlwapi', 'user32', 'version', 'ws2_32']
        self.cpp_info.set_property("cmake_file_name", "BrowserControlSDK")
        self.cpp_info.set_property("cmake_target_name", "BrowserControlSDK::sdk")
