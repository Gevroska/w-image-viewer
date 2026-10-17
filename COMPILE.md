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

Beta tags such as `v2.2.0-beta.1` must match `WIV_VERSION_STRING` in the version
header and `version-semver` in the manifest. Numeric Windows resources keep
the base version, while FileVersion, ProductVersion, and the About dialog show
the full beta version. Beta releases use `--prerelease --latest=false` and leave
the latest stable release unchanged.

### Building from source on Windows

Install Visual Studio 2022 with the Desktop development with C++ workload,
the MSVC v143 toolset, and a Windows SDK. The workflow overrides the project's
v145 setting with v143, so it can build on the Windows 2022 runner.

Dependencies are declared in `vcpkg.json`: Dear ImGui (DirectX 11 and Win32
bindings), Little CMS, LibRaw, and OpenImageIO with the image codecs used by
the application. The vcpkg baseline is pinned to the same revision as CI.

`vcpkg-configuration.json` selects the release-only static triplet and the
OpenImageIO overlay in `.github/vcpkg-overlay/openimageio`. That overlay is
copied from the pinned vcpkg baseline and adds `viewer-input-fixes.patch` for
JPEG XL sample depths, color/alpha channel selection, ICC metadata, first-frame
selection, and HEIF/AVIF ICC/NCLX metadata. AVIF decoding is enabled through
`libheif[aom]`; JPEG XL uses OpenImageIO's `jpegxl` feature. Overlay contents
are included in the CI dependency-cache key.

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

### Regression tests

CI builds `tests/wiv-tests.vcxproj` with the same static dependencies and runs:

```powershell
.\build\wiv-tests.exe .\tests\fixtures .\build\test-data
```

The suite exercises all eight orientation permutations, independent JPEG/PNG/
TIFF/AVIF reference images, gray/alpha handling, JPEG XL 8/10/12/16-bit data,
native JXL orientations, ICC profiles, navigation visibility timing, and
mixed-format directory traversal with corrupt neighbors and Unicode paths.
Fixtures and pixel references are committed. `tests/generate_fixtures.py`
regenerates the independent references with Pillow 12.3.0; Pillow is not a
runtime dependency of the viewer or test executable.

Beta.2 also tests configuration persistence, disabled EXIF/camera transforms,
original synthetic DNG full-image/JPEG-preview fixtures, monitor-based alignment,
and real Win32 fullscreen/placement round trips. `tests/generate_raw_fixtures.py`
regenerates the DNG files with the Python standard library.

On a local Windows desktop with a DirectX 11 adapter, pass the application as
the third argument to also verify actual startup maximization, disabled
maximization, fullscreen precedence, failed opens, and minimized launches:

```powershell
.\build\wiv-tests.exe .\tests\fixtures .\build\test-data .\build\w-image-viewer.exe
```

These startup tests create isolated executable/configuration copies under the
test-data directory. They do not use the installed viewer's configuration.
