"""Generate independent decoder references with Pillow (fixtures are committed)."""
from pathlib import Path
from PIL import Image, ImageCms, ImageOps

root = Path(__file__).parent / 'fixtures'
root.mkdir(exist_ok=True)
image = Image.new('RGB', (64, 40))
colors = [(240, 20, 30), (20, 220, 40), (30, 40, 230), (240, 220, 30)]
for y in range(image.height):
    for x in range(image.width):
        image.putpixel((x, y), colors[(y >= 20) * 2 + (x >= 32)])
icc = ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()

def reference(path):
    decoded = ImageOps.exif_transpose(Image.open(path)).convert('RGBA')
    pixels = bytearray(decoded.tobytes())
    for i in range(0, len(pixels), 4):
        for c in range(3):
            pixels[i+c] = (pixels[i+c] * pixels[i+3] + 127) // 255
    path.with_suffix(path.suffix + '.rgba').write_bytes(pixels)
    return decoded.size

for suffix in ('png', 'jpg', 'tiff'):
    for orientation in range(1, 9):
        exif = Image.Exif()
        exif[274] = orientation
        path = root / f'orientation-{orientation}.{suffix}'
        options = dict(exif=exif, icc_profile=icc)
        if suffix == 'jpg': options.update(quality=100, subsampling=0)
        image.save(path, **options)
        reference(path)
image.save(root / 'plain.webp', lossless=True)
reference(root / 'plain.webp')
alpha = image.convert('RGBA')
for y in range(alpha.height):
    for x in range(alpha.width):
        r, g, b, _ = alpha.getpixel((x, y))
        alpha.putpixel((x, y), (r, g, b, (0, 85, 170, 255)[x // 16]))
for suffix in ('png', 'avif'):
    path = root / f'alpha.{suffix}'
    alpha.save(path, quality=100, subsampling='4:4:4', icc_profile=icc)
    reference(path)
image.save(root / 'plain.avif', quality=100, subsampling='4:4:4', icc_profile=icc)
reference(root / 'plain.avif')
for orientation in (2, 6, 7, 8):
    exif = Image.Exif()
    exif[274] = orientation
    path = root / f'orientation-{orientation}.avif'
    image.save(path, quality=100, subsampling='4:4:4', exif=exif, icc_profile=icc)
    reference(path)
image.convert('LA').save(root / 'gray-alpha.png')
reference(root / 'gray-alpha.png')
(root / 'corrupt.png').write_bytes(b'not an image')
(root / 'srgb.icc').write_bytes(icc)
print(f'Generated {len(list(root.iterdir()))} fixtures and reference files.')
