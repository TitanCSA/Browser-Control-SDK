# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | browser-control-sdk |
| CPP/conan | published_custom_source_index | [browser-control-sdk](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | published_payload_verified | [Browser-Control-SDK.Native](https://www.nuget.org/packages/Browser-Control-SDK.Native/0.2.0) |
| NET/nuget | published_payload_verified | [BrowserControlSDK.SDK](https://www.nuget.org/packages/BrowserControlSDK.SDK/1.0.0) |
| Python/pypi | published_install_verified | [browser-control-sdk](https://pypi.org/project/browser-control-sdk/0.2.1/) |
| Python/conda-forge | submitted_pending_maintainer_review | browser-control-sdk [Source](Browser-Control-SDK-Python-conda-forge/) |
| Rust/cargo | published_install_verified | [browser-control-sdk](https://crates.io/crates/browser-control-sdk) [Source](Browser-Control-SDK-Rust-cargo/) |
| JavaScript/npm | published_install_verified | [browser-control-sdk](https://www.npmjs.com/package/browser-control-sdk) [Source](Browser-Control-SDK-JavaScript-npm/) |
| Java/maven | published_install_verified | [browser-control-sdk](https://central.sonatype.com/artifact/io.github.titancsa/browser-control-sdk/1.0.0) [Source](Browser-Control-SDK-Java-maven/) |
| Java/jitpack | published_install_verified | [browser-control-sdk](https://jitpack.io/#TitanCSA/Browser-Control-SDK/java-sdk-v1.0.0) [Source](Browser-Control-SDK-Java-maven/) |
| Go/modules | published_install_verified | [github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules](https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1) [Source](Browser-Control-SDK-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1) |
| PowerShell/gallery | published_install_verified | [BrowserControlSDK](https://www.powershellgallery.com/packages/BrowserControlSDK/0.2.1) [Source](Browser-Control-SDK-PowerShell-gallery/) |
| Dart/pub-dev | published_install_verified | [browser_control_sdk](https://pub.dev/packages/browser_control_sdk) [Source](Browser-Control-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [browser-control-sdk](https://rubygems.org/gems/browser-control-sdk) [Source](Browser-Control-SDK-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/browser-control-sdk](https://packagist.org/packages/titancsa/browser-control-sdk) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

C ABI header, import library and native DLL: [Windows x64 SDK archive](https://github.com/TitanCSA/Browser-Control-SDK/releases/tag/c-abi-sdk-v0.2.2). This GitHub archive is separate from central-registry packages.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
