## Compilation

### Automated Windows builds

The [Windows workflow](https://github.com/Gevroska/w-image-viewer/actions/workflows/windows.yml)
builds a Release executable for Windows x64 on pushes to `master`, pull requests,
and manual runs. Each successful build uploads the executable, a portable ZIP,
license notices, and SHA-256 checksums as a downloadable artifact.

Pushing a version tag such as `v2.1.1` also publishes these files in GitHub
Releases. The tag must match `w-image-viewer/src/resources/version.h` and the
version in `vcpkg.json`. Update both files before tagging a new version:

```powershell
git tag v2.1.1
git push origin v2.1.1
```

### Building from source on Windows

Install Visual Studio 2022 with the Desktop development with C++ workload,
the MSVC v143 toolset, and a Windows SDK. The workflow overrides the project's
v145 setting with v143, so it can build on the Windows 2022 runner.

Dependencies are declared in `vcpkg.json`: Dear ImGui (DirectX 11 and Win32
bindings), Little CMS, LibRaw, and OpenImageIO with the image codecs used by
the application. The vcpkg baseline is pinned to the same revision as CI.

From the repository root, in a Developer PowerShell for Visual Studio:

```powershell
git clone https://github.com/microsoft/vcpkg.git .vcpkg
git -C .vcpkg checkout 66c0373dc7fca549e5803087b9487edfe3aca0a1
.\.vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\.vcpkg\vcpkg.exe install --triplet x64-windows-static --overlay-triplets=.github/vcpkg-triplets
.\.vcpkg\vcpkg.exe integrate install
msbuild w-image-viewer.sln /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:VcpkgEnableManifest=true /p:VcpkgManifestInstall=false /p:VcpkgTriplet=x64-windows-static "/p:VcpkgInstalledDir=$PWD/vcpkg_installed/" "/p:OutDir=$PWD/build/"
```

The executable is written to `build/w-image-viewer.exe`. The CI triplet builds
only Release libraries with static linkage. LibRaw's OpenMP support still
requires the Microsoft Visual C++ x64 Redistributable at runtime.
