# OpenImageIO viewer input fixes

The original port files are from microsoft/vcpkg revision
`66c0373dc7fca549e5803087b9487edfe3aca0a1` (OpenImageIO 3.0.9.1#3).
`viewer-input-fixes.patch` contains this fork's input integration changes.

JPEG XL decoding selects color plus optional alpha, supports integer depths
through 16 bits, exposes the decoded-data ICC profile and alpha association,
retains libjxl's normalized orientation, and returns the first complete frame.
It also accepts small valid codestreams and checks output dimensions and sizes.

HEIF/AVIF decoding exposes embedded ICC data and SDR NCLX primaries/transfer
metadata. Container transformations remain owned by libheif. The application
handles channel normalization, premultiplication, and the remaining EXIF
orientation transforms before GPU upload. AVIF output remains 8-bit.

Keep the upstream source/license notices and the original port patches when
updating this overlay. Test real JXL/AVIF decoding with the packaged build.

`viewer-tiff-orientation.patch` makes the libtiff RGBA fallback return stored
pixel order like the normal scanline path. This prevents libtiff from consuming
part of an EXIF transform before the application normalizes all eight values.
