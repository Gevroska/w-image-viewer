param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'

$exe = Get-Item 'build/w-image-viewer.exe'
$bytes = [System.IO.File]::ReadAllBytes($exe.FullName)
if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Missing executable header.' }
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) {
    throw 'Expected an x64 Windows PE executable.'
}
$fileVersion = $exe.VersionInfo
$actualVersion = '{0}.{1}.{2}' -f $fileVersion.FileMajorPart, $fileVersion.FileMinorPart, $fileVersion.FileBuildPart
if ($actualVersion -cne $Version) { throw "Executable version $actualVersion does not match $Version." }

New-Item -ItemType Directory -Force artifacts, package | Out-Null
Copy-Item $exe.FullName artifacts/w-image-viewer.exe
Copy-Item $exe.FullName package/w-image-viewer.exe
Copy-Item LICENSE.txt, MANUAL.md, README.md package/

$notices = foreach ($directory in Get-ChildItem vcpkg_installed/x64-windows-static/share -Directory | Sort-Object Name) {
    $copyright = Join-Path $directory.FullName 'copyright'
    if (Test-Path $copyright) {
        "===== $($directory.Name) ====="
        Get-Content $copyright -Raw
        ''
    }
}
$notices | Set-Content artifacts/THIRD-PARTY-LICENSES.txt -Encoding utf8
Copy-Item artifacts/THIRD-PARTY-LICENSES.txt package/
Copy-Item LICENSE.txt artifacts/

$archive = "artifacts/w-image-viewer-$Version-windows-x64.zip"
Compress-Archive -Path package/* -DestinationPath $archive -Force
$checksums = foreach ($file in Get-ChildItem artifacts -File | Sort-Object Name) {
    '{0}  {1}' -f (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $file.Name
}
$checksums | Set-Content artifacts/SHA256SUMS.txt -Encoding ascii
"Verified Windows x64 executable version $actualVersion ($($exe.Length) bytes)."
