# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | browser-control-sdk-sdk |
| CPP/conan | public_recipe_builds_locally |  |
| CPP/nuget | paused_pending_owner_email_confirmation | Browser-Control-SDK.Native |
| NET/nuget | binding_not_implemented |  |
| Python/pypi | published_install_verified | [browser-control-sdk](https://pypi.org/project/browser-control-sdk/0.2.1/) |
| Python/conda-forge | submitted_pending_maintainer_review | browser-control-sdk |
| Rust/cargo | binding_not_implemented |  |
| JavaScript/npm | published_install_verified | [browser-control-sdk](https://www.npmjs.com/package/browser-control-sdk) [Source](Browser-Control-SDK-JavaScript-npm/) |
| Java/maven | binding_not_implemented |  |
| Java/jitpack | binding_not_implemented |  |
| Go/modules | published_install_verified | [github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules](https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1) [Source](Browser-Control-SDK-Go-modules/) |
| Go/pkg-go-dev | indexing_pending | [Documentation](https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1) |
| PowerShell/gallery | published_install_verified | [BrowserControlSDK](https://www.powershellgallery.com/packages/BrowserControlSDK/0.2.1) [Source](Browser-Control-SDK-PowerShell-gallery/) |
| Dart/pub-dev | published_install_verified | [browser_control_sdk](https://pub.dev/packages/browser_control_sdk) [Source](Browser-Control-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [browser-control-sdk](https://rubygems.org/gems/browser-control-sdk) [Source](Browser-Control-SDK-Ruby-rubygems/) |
| PHP/packagist | binding_not_implemented |  |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
