![](logo.png)

# W Image Viewer

**W Image Viewer** is a simple yet powerful image viewer.

## Key features

- Extensive image format support (including RAW images).
- Advanced image scaling capabilities.
- Color management.
- GPU rendering (using DirectX 11).
- Simple to use.
- Fast and lite.
- Portable.

## Requirements to run

Microsoft Visual C++ 2022 Redistributable x64

## Download

Download the Windows x64 executable or portable ZIP from the
[latest release](https://github.com/Gevroska/w-image-viewer/releases/latest).
The ZIP includes the manual and license notices. SHA-256 checksums are provided
with each release.

Builds of the latest source are also available as downloadable artifacts in
[GitHub Actions](https://github.com/Gevroska/w-image-viewer/actions/workflows/windows.yml).

## 2.2.0 beta

[2.2.0-beta.2](https://github.com/Gevroska/w-image-viewer/releases/tag/v2.2.0-beta.2)
adds all eight EXIF orientations, JPEG XL and AVIF decoding, and previous/next
buttons at the sides of the viewer. Moving the mouse reveals the buttons;
they fade after two seconds of inactivity and stay visible while hovered.
Folder navigation includes all supported image formats in filename order.

Beta.2 adds configurable maximization on explicit file opens, fixes fullscreen
to use one monitor and restore window placement, and adds EXIF/camera orientation
control with optional long-edge alignment to the monitor. Manual rotation takes
priority over automatic alignment for the current image. Existing Fit upscaling
and original-size controls are preserved.

The beta is published as a prerelease; 2.1.1 remains the latest stable release.
The renderer supports SDR still images. AVIF currently decodes to 8-bit RGB(A).
JPEG XL animations show their first complete frame; animation playback and HDR
output are not part of this beta.

## 

[User manual](MANUAL.md)  
[Compilation](COMPILE.md)
