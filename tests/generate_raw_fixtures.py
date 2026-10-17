"""Generate small, original CFA DNG fixtures using only the Python standard library.

The JPEG previews come from this repository's independently generated JPEG fixtures.
No camera photographs or external sample-data license is required.
"""
from pathlib import Path
import struct

DEST = Path(__file__).parent / 'fixtures'
WIDTH, HEIGHT = 128, 96


def tag(number, kind, values):
    if kind == 2:
        data = values.encode('ascii') + b'\0'
        count = len(data)
    elif kind in (5, 10):
        count = len(values)
        data = struct.pack('<' + ('I' if kind == 5 else 'i') * count * 2,
                           *(item for pair in values for item in pair))
    elif isinstance(values, str):
        count, data = 1, values  # Deferred blob offset.
    else:
        if isinstance(values, int):
            values = [values]
        count = len(values)
        data = struct.pack('<' + {1: 'B', 3: 'H', 4: 'I'}[kind] * count, *values)
    return number, kind, count, data


def make_dng(path, orientation, jpeg=None):
    raw = b''.join(struct.pack('<H',
        (600 + x * 16 if y % 2 == 0 and x % 2 == 0 else
         300 + y * 20 if y % 2 and x % 2 else 400 + x * 8 + y * 8))
        for y in range(HEIGHT) for x in range(WIDTH))
    camera = [tag(271, 2, 'WIV'), tag(272, 2, 'Synthetic CFA fixture'),
              tag(274, 3, orientation), tag(50706, 1, [1, 4, 0, 0]),
              tag(50707, 1, [1, 1, 0, 0]), tag(50708, 2, 'WIV Synthetic CFA'),
              tag(50721, 10, [(1, 1), (0, 1), (0, 1), (0, 1), (1, 1),
                              (0, 1), (0, 1), (0, 1), (1, 1)]),
              tag(50728, 5, [(1, 1)] * 3), tag(50778, 3, 21)]
    full = [tag(254, 4, 0), tag(256, 4, WIDTH), tag(257, 4, HEIGHT), tag(258, 3, 16),
            tag(259, 3, 1), tag(262, 3, 32803), tag(273, 4, 'raw'),
            tag(274, 3, orientation), tag(277, 3, 1), tag(278, 4, HEIGHT),
            tag(279, 4, len(raw)), tag(284, 3, 1), tag(33421, 3, [2, 2]),
            tag(33422, 1, [0, 1, 1, 2]), tag(50710, 1, [0, 1, 2]), tag(50711, 3, 1),
            tag(50713, 3, [1, 1]), tag(50714, 5, [(0, 1)]), tag(50717, 4, 4095)]
    if jpeg is None:
        # Orientation occurs once in the root IFD.
        tables = [[item for item in full if item[0] != 274] + camera]
    else:
        preview = [tag(254, 4, 1), tag(256, 4, 64), tag(257, 4, 40),
                   tag(258, 3, [8, 8, 8]), tag(259, 3, 6), tag(262, 3, 6),
                   tag(273, 4, 'jpeg'), tag(277, 3, 3), tag(278, 4, 40),
                   tag(279, 4, len(jpeg)), tag(284, 3, 1),
                   tag(330, 4, 'subifd'), tag(513, 4, 'jpeg'), tag(514, 4, len(jpeg))]
        tables = [preview + camera, full]
    tables = [sorted(table) for table in tables]
    offsets, end = [], 8
    for table in tables:
        offsets.append(end)
        end += 2 + len(table) * 12 + 4
    data = bytearray(b'II' + struct.pack('<HI', 42, 8) + b'\0' * (end - 8))
    deferred = []
    for offset, table in zip(offsets, tables):
        struct.pack_into('<H', data, offset, len(table))
        for index, (number, kind, count, value) in enumerate(table):
            entry = offset + 2 + index * 12
            struct.pack_into('<HHI', data, entry, number, kind, count)
            if isinstance(value, str):
                deferred.append((entry + 8, value))
            elif len(value) <= 4:
                data[entry + 8:entry + 8 + len(value)] = value
            else:
                if len(data) % 2:
                    data.append(0)
                struct.pack_into('<I', data, entry + 8, len(data))
                data.extend(value)
    blobs = {'subifd': offsets[-1]}
    for name, blob in [('jpeg', jpeg), ('raw', raw)]:
        if blob is not None:
            if len(data) % 2:
                data.append(0)
            blobs[name] = len(data)
            data.extend(blob)
    for entry, name in deferred:
        struct.pack_into('<I', data, entry, blobs[name])
    path.write_bytes(data)


if __name__ == '__main__':
    for orientation in (1, 3, 6, 8):
        make_dng(DEST / f'raw-full-{orientation}.dng', orientation)
        make_dng(DEST / f'raw-preview-{orientation}.dng', orientation,
                 (DEST / f'orientation-{orientation}.jpg').read_bytes())
    make_dng(DEST / 'raw-broken-preview.dng', 6, b'\xff\xd8\xff\xd9')
    print('Generated original full-CFA and JPEG-preview DNG fixtures.')
