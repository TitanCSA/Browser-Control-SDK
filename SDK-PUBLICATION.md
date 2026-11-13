# Browser-Control-SDK — SDK et installation

Projet : [Browser-Control-SDK](https://github.com/TitanCSA/Browser-Control-SDK). [Sources et documentation des SDK](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Browser-Control-SDK).

Choisis la ligne correspondant au langage de ton application. Installe son outil habituel (Node.js/npm, Python/pip, .NET, etc.), puis execute la commande dans le dossier de ton application. Les pilotes et applications requis restent a installer separement selon le README du projet.

Les SDK natifs sont destines a Windows x64. Java utilise Java22+ (Kotlin passe par le SDK Java). Flutter utilise le SDK Dart ; le chargement de la DLL doit etre configure dans l application. AutoHotInterceptor .NET utilise .NET Framework4.8.

| Langage / registre | Disponibilite | Installation / utilisation |
| --- | --- | --- |
| C++ / vcpkg | Registre TitanCSA disponible (configuration ci-dessous) | `vcpkg install browser-control-sdk:x64-windows` |
| C++ / Conan | Registre TitanCSA disponible (configuration ci-dessous) | `conan install --requires=browser-control-sdk/0.2.0 -r titancsa --build=missing -s compiler.cppstd=17` |
| C++ / NuGet natif | Publie et verifie | `Install-Package Browser-Control-SDK.Native -Version 0.2.0` |
| C# / .NET | Publie et verifie | `dotnet add package BrowserControlSDK.SDK --version 1.0.0` |
| Python / pip | Publie et verifie | `python -m pip install browser-control-sdk==0.2.1` |
| Python / Conda | Canal TitanCSA disponible ; acceptation conda-forge encore en attente | `conda install --override-channels -c https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel -c conda-forge browser-control-sdk` |
| Rust / Cargo | Publie et verifie | `cargo add browser-control-sdk@1.0.0` |
| JavaScript et TypeScript / npm | Publie et verifie | `npm install browser-control-sdk@0.2.0` |
| Java et Kotlin / Maven Central | Publie et verifie | `io.github.titancsa:browser-control-sdk:1.0.0 (pom.xml / Gradle)` |
| Java et Kotlin / JitPack | Publie et verifie | `com.github.TitanCSA:Browser-Control-SDK:java-sdk-v1.0.0 (avec le depot JitPack)` |
| Go / Go Modules | Publie et verifie | `go get github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1` |
| Go / pkg.go.dev | Publie et verifie | `Documentation : https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1` |
| PowerShell / Gallery | Publie et verifie | `Install-Module BrowserControlSDK -RequiredVersion 0.2.1 -Scope CurrentUser` |
| Dart et Flutter / pub.dev | Publie et verifie | `dart pub add browser_control_sdk:0.2.1` |
| Ruby / RubyGems | Publie et verifie | `gem install browser-control-sdk -v 0.2.2` |
| PHP / Composer | Pas encore disponible / validation en cours | Commande prevue apres publication : `composer require titancsa/browser-control-sdk:1.0.0` |

## C et C++

Les sources et headers C / C++ sont dans le dossier central de ce projet. Les headers C se trouvent dans les dossiers `*-C-ffi` ou `*-C-json` (Interceptor-MultiLang : `native/`). Les paquets natifs C++ et la disponibilite du bridge C sont distincts ; consulte leur README avant de lier une application C.

Pour vcpkg, ajoute un fichier `vcpkg-configuration.json` a cote de ton manifeste `vcpkg.json` :

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/TitanCSA/vcpkg-registry",
      "baseline": "9a4b8cb920d4cfc97ce6d7af461bcc7508392a68",
      "packages": [
        "browser-control-sdk",
        "browser-control-sdk-sdk"
      ]
    }
  ]
}
```

Pour Conan2, configure une seule fois l index source :

```powershell
git clone https://github.com/TitanCSA/sdk-packages.git
conan remote add titancsa ./sdk-packages/conan-index -t local-recipes-index
```

Execute ensuite la commande Conan du tableau. Ce registre construit les bibliotheques depuis leurs sources ; il est distinct de ConanCenter. La commande NuGet native s execute dans la console du gestionnaire de packages de Visual Studio.

## Java et Kotlin

Dans le bloc `<dependencies>` de ton `pom.xml` :

```xml
<dependency>
  <groupId>io.github.titancsa</groupId>
  <artifactId>browser-control-sdk</artifactId>
  <version>1.0.0</version>
</dependency>
```

Avec Gradle Kotlin :

```kotlin
implementation("io.github.titancsa:browser-control-sdk:1.0.0")
```

Pour JitPack, ajoute aussi le depot `https://jitpack.io`, puis utilise les coordonnees du tableau. Les versions Go et JitPack deja publiees continuent de pointer vers leurs anciens tags immuables dans le depot du projet : ces tags restent disponibles. Leurs sources de travail sont maintenant centralisees ici ; aucun ancien import Go n est renomme.

## Etat et documentation

Les commandes marquees « prevues » ne sont pas encore des installations disponibles. Le canal Conda TitanCSA n est pas le catalogue officiel conda-forge. Les versions et licences existantes ne sont pas remplacees par cette reorganisation.

Les guides API et exemples de chaque langage se trouvent dans leurs dossiers SDK, a cote des sources. Les bindings peuvent exposer une partie de l API C++ ; les limites restent documentees dans chaque SDK.

[Retour au catalogue central](../../README.md)
