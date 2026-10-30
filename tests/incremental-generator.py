#!/usr/bin/env python3
"""Round-trip structured NRO/VPK fixtures through the production recipe decoder."""
import importlib.util
import io
import json
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

project = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', project / 'tools/make-incremental.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
reader = project / 'build/incremental-tests/reader'
if not reader.exists():
    subprocess.run(['python3', str(project / 'tests/incremental.py')], check=True)


def nro(version, data, title=b'Tweebbo'):
    header = bytearray(128);header[0x10:0x14] = b'NRO0';struct.pack_into('<I', header, 0x18, 128)
    nacp = bytearray(16384);nacp[:len(title)] = title;nacp[0x3060:0x3060 + len(version)] = version.encode()
    directory = struct.pack('<6I', 0, 0xffffffff, 0xffffffff, 0, 0xffffffff, 0)
    files = bytearray()
    for name, sibling, count in [(b'same', 36, len(data)), (b'empty', 0xffffffff, 0)]:
        files.extend(struct.pack('<IIQQII', 0, sibling, 0, count, 0xffffffff, len(name)) + name)
        files.extend(b'\0' * (-len(files) % 4))
    level = struct.pack('<10Q', 80, 0, 0, 80, len(directory), 0, 0, 104, len(files), 512)
    romfs = (level + directory + files).ljust(512, b'\0') + data
    aset = b'ASET\0\0\0\0' + struct.pack('<6Q', 56, 0, 56, len(nacp), 56 + len(nacp), len(romfs))
    return bytes(header) + aset + nacp + romfs


def sfo(version, identity='TWEE00001'):
    major, minor, micro = map(int, version.split('.'))
    fields = {'APP_VER': f'{major:02d}.{minor * 10 + micro:02d}', 'TITLE': 'Tweebbo', 'TITLE_ID': identity}
    index, keys, values = bytearray(), bytearray(), bytearray()
    for key, value in fields.items():
        data = value.encode() + b'\0';length = (len(data) + 3) // 4 * 4
        index.extend(struct.pack('<HHIII', len(keys), 0x204, len(data), length, len(values)))
        keys.extend(key.encode() + b'\0');values.extend(data.ljust(length, b'\0'))
    start = 20 + len(index);values_at = (start + len(keys) + 3) // 4 * 4
    return struct.pack('<5I', 0x46535000, 0x101, start, values_at, len(fields)) + index + keys.ljust(values_at - start, b'\0') + values


def vpk(version, data, extra=None, compression=zipfile.ZIP_STORED, identity='TWEE00001'):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=compression) as archive:
        archive.writestr('eboot.bin', b'code ' + version.encode())
        archive.writestr('sce_sys/param.sfo', sfo(version, identity))
        archive.writestr('gfx/same.png', data)
        archive.writestr('empty', b'')
        if extra:
            archive.writestr(extra, b'bad')
    return buffer.getvalue()


checks = []
with tempfile.TemporaryDirectory(prefix='portable-generator-') as temporary:
    root = Path(temporary)
    original = bytes(range(256)) * 512
    for platform, make, extension in [('switch', nro, '.nro'), ('vita', vpk, '.vpk')]:
        base = root / ('base' + extension);base.write_bytes(make('1.4.3', original))
        old = builder.entries(base, platform, '1.4.3')
        source = root / platform;source.mkdir()
        raw = base.read_bytes()
        for name, entry in old.items():
            p = source / name;p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(raw[entry['offset']:entry['offset'] + entry['bytes']])
        for label, data in [('same', original), ('changed', b'changed asset' * 9000)]:
            target, patch, output = root / ('target' + extension), root / 'update.tupd', root / 'rebuilt'
            target.write_bytes(make('1.4.4', data))
            report = builder.build(base, target, patch, platform, '1.4.3', '1.4.4')
            assert report['reusedBytes'] == (len(original) if label == 'same' else 0)
            result = subprocess.run([str(reader), 'apply', str(patch), str(output), str(source), report['identity'],
                                     '1.4.3', '1.4.4', report['targetSha256'], str(report['targetBytes']),
                                     '0' if platform == 'switch' else '1', report['sha256'], str(report['bytes'])],
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert output.read_bytes() == target.read_bytes()
            checks.append(platform + ' ' + label + ' including empty shared-offset file')
    bad = [
        ('switch', '.nro', nro('1.4.4', original, b'Other game'), 'identity'),
        ('switch', '.nro', nro('1.4.3', original), 'version'),
        ('vita', '.vpk', vpk('1.4.4', original, extra='../outside'), 'unsafe path'),
        ('vita', '.vpk', vpk('1.4.4', original, compression=zipfile.ZIP_DEFLATED), 'unsupported compression'),
        ('vita', '.vpk', vpk('1.4.4', original, identity='OTHER0001'), 'identity'),
    ]
    for platform, extension, data, label in bad:
        file = root / ('bad' + extension);file.write_bytes(data)
        try:
            builder.entries(file, platform, '1.4.4')
            raise AssertionError('Accepted ' + platform + ' ' + label)
        except ValueError:
            checks.append('rejected ' + platform + ' ' + label)
print('PASS portable generator:', len(checks), 'structured round-trip/identity/path/compression cases')
(project / 'build/incremental-tests/generator.json').write_text(json.dumps({'status': 'PASS', 'checks': checks}, indent=2) + '\n')
