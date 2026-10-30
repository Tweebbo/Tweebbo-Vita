#!/usr/bin/env python3
"""Build a TUPD1 recipe from installed Switch RomFS or stored Vita VPK files."""
import argparse
import hashlib
import json
import struct
import zipfile
import zlib
from pathlib import Path

MAX_BYTES = 512 * 1024 * 1024
PLATFORMS = {'switch': (3, 'TWEEBBO-SWITCH__', '.nro'), 'vita': (4, 'TWEEBBO-VITA____', '.vpk')}


def checksum(path, offset=0, count=None):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        source.seek(offset)
        left = path.stat().st_size - offset if count is None else count
        while left:
            data = source.read(min(65536, left))
            if not data:
                raise ValueError('Truncated artifact')
            digest.update(data)
            left -= len(data)
    return digest.hexdigest()


def safe_path(value):
    return (0 < len(value.encode()) <= 240 and
            all(32 <= ord(c) <= 126 and c not in '\\:' for c in value) and
            all(part not in ['', '.', '..'] for part in value.split('/')))


def version(value):
    parts = value.split('.')
    if len(value) > 23 or len(parts) != 3 or any(not p.isascii() or not p.isdigit() for p in parts):
        raise ValueError('Use a semantic version')
    return tuple(map(int, parts))


def sfo_fields(data):
    if len(data) < 20 or data[:4] != b'\0PSF':
        raise ValueError('Invalid Vita application metadata')
    keys, values, count = struct.unpack_from('<III', data, 8)
    if count > 128 or 20 + count * 16 > len(data):
        raise ValueError('Invalid Vita metadata index')
    result = {}
    for i in range(count):
        name, fmt, length, maximum, offset = struct.unpack_from('<HHIII', data, 20 + i * 16)
        start = keys + name
        end = data.find(b'\0', start)
        if start < keys or end < start or end >= values or length > maximum or values + offset + length > len(data):
            raise ValueError('Invalid Vita metadata entry')
        key = data[start:end].decode('ascii')
        if key in result:
            raise ValueError('Duplicate Vita metadata key')
        result[key] = data[values + offset:values + offset + length].rstrip(b'\0')
    return result


def entries(path, platform, expected_version):
    size = path.stat().st_size
    if not 80 <= size <= MAX_BYTES:
        raise ValueError('Artifact outside the supported size range')
    result = {}
    with path.open('rb') as source:
        def region(offset, length):
            if offset < 0 or length < 0 or offset + length > size:
                raise ValueError('Artifact region outside the file')
            source.seek(offset)
            data = source.read(length)
            if len(data) != length:
                raise ValueError('Truncated artifact region')
            return data

        if platform == 'vita':
            with zipfile.ZipFile(path) as archive:
                files = archive.infolist()
                if len(files) > 16384:
                    raise ValueError('Too many VPK entries')
                for item in files:
                    if item.is_dir():
                        continue
                    if (not safe_path(item.filename) or item.filename in result or item.flag_bits & 1 or
                            item.compress_type != zipfile.ZIP_STORED or item.compress_size != item.file_size or
                            (item.external_attr >> 16) & 0o170000 == 0o120000):
                        raise ValueError('VPK entries must be ordinary, unencrypted ZIP_STORED files')
                    header = region(item.header_offset, 30)
                    if header[:4] != b'PK\x03\x04':
                        raise ValueError('Invalid VPK local header')
                    name, extra = struct.unpack_from('<HH', header, 26)
                    if region(item.header_offset + 30, name).decode('utf-8') != item.filename:
                        raise ValueError('VPK local and central names differ')
                    absolute = item.header_offset + 30 + name + extra
                    if absolute + item.file_size > size:
                        raise ValueError('VPK entry outside the file')
                    result[item.filename] = {'offset': absolute, 'bytes': item.file_size,
                                             'sha256': checksum(path, absolute, item.file_size)}
                if archive.getinfo('sce_sys/param.sfo').file_size > 65536:
                    raise ValueError('Vita application metadata is too large')
                metadata = sfo_fields(archive.read('sce_sys/param.sfo'))
                major, minor, micro = version(expected_version)
                if (metadata.get('TITLE_ID') != b'TWEE00001' or metadata.get('TITLE') != b'Tweebbo' or
                        metadata.get('APP_VER') != f'{major:02d}.{minor * 10 + micro:02d}'.encode()):
                    raise ValueError('VPK identity or version differs from the release')
            return result

        header = region(0, 0x80)
        if header[0x10:0x14] != b'NRO0':
            raise ValueError('Missing NRO header')
        executable = struct.unpack_from('<I', header, 0x18)[0]
        asset = region(executable, 56)
        if asset[:8] != b'ASET\0\0\0\0':
            raise ValueError('Missing NRO asset section')
        nacp_offset, nacp_size, romfs_offset, romfs_size = struct.unpack_from('<4Q', asset, 24)
        if not 0x3070 <= nacp_size <= 65536 or romfs_size > MAX_BYTES:
            raise ValueError('Invalid NRO metadata or RomFS size')
        nacp = region(executable + nacp_offset, nacp_size)
        if nacp[:512].split(b'\0', 1)[0] != b'Tweebbo' or nacp[0x3060:0x3070].split(b'\0', 1)[0].decode() != expected_version:
            raise ValueError('NRO identity or version differs from the release')
        start = executable + romfs_offset
        if start + romfs_size > size:
            raise ValueError('NRO RomFS outside the file')
        header = region(start, 80)
        if struct.unpack_from('<Q', header)[0] != 80:
            raise ValueError('Invalid Switch RomFS header')
        directory_offset, directory_size = struct.unpack_from('<QQ', header, 24)
        file_offset, file_size, payload = struct.unpack_from('<QQQ', header, 56)
        if directory_offset + directory_size > romfs_size or file_offset + file_size > romfs_size or payload > romfs_size:
            raise ValueError('RomFS metadata outside its partition')
        if directory_size > 8 * 1024 * 1024 or file_size > 8 * 1024 * 1024:
            raise ValueError('RomFS metadata exceeds its limit')
        directories = region(start + directory_offset, directory_size)
        files = region(start + file_offset, file_size)
        seen_directories, seen_files = set(), set()

        def walk(offset, parent, depth=0):
            if depth > 32 or offset in seen_directories or offset + 24 > len(directories):
                raise ValueError('Invalid RomFS directory graph')
            seen_directories.add(offset)
            length = struct.unpack_from('<I', directories, offset + 20)[0]
            if offset + 24 + length > len(directories):
                raise ValueError('Invalid RomFS directory name')
            name = directories[offset + 24:offset + 24 + length].decode('ascii')
            if name and ('/' in name or not safe_path(name)):
                raise ValueError('Unsafe RomFS directory name')
            prefix = parent + (name + '/' if name else '')
            child = struct.unpack_from('<I', directories, offset + 12)[0]
            while child != 0xffffffff:
                if child in seen_files or child + 32 > len(files):
                    raise ValueError('Invalid RomFS file graph')
                seen_files.add(child)
                length = struct.unpack_from('<I', files, child + 28)[0]
                if child + 32 + length > len(files):
                    raise ValueError('Invalid RomFS file name')
                name = files[child + 32:child + 32 + length].decode('ascii')
                full = prefix + name
                if '/' in name or not safe_path(full) or full in result:
                    raise ValueError('Unsafe or duplicate RomFS path')
                position, count = struct.unpack_from('<QQ', files, child + 8)
                absolute = start + payload + position
                if absolute + count > start + romfs_size:
                    raise ValueError('RomFS payload outside the file')
                result[full] = {'offset': absolute, 'bytes': count, 'sha256': checksum(path, absolute, count)}
                child = struct.unpack_from('<I', files, child + 4)[0]
            child = struct.unpack_from('<I', directories, offset + 8)[0]
            while child != 0xffffffff:
                walk(child, prefix, depth + 1)
                child = struct.unpack_from('<I', directories, child + 4)[0]

        walk(0, '')
    return result


def build(base, target, output, platform, source_version, target_version):
    kind, identity, suffix = PLATFORMS[platform]
    if base.suffix != suffix or target.suffix != suffix or output.resolve() in [base.resolve(), target.resolve()]:
        raise ValueError('Use distinct output and matching NRO or VPK inputs')
    if version(target_version) <= version(source_version):
        raise ValueError('The target must be newer than its base')
    old, new = entries(base, platform, source_version), entries(target, platform, target_version)
    operations, position, reused = [], 0, 0
    for name, item in sorted(new.items(), key=lambda pair: pair[1]['offset']):
        start, count = item['offset'], item['bytes']
        if not count:
            continue
        if start < position:
            raise ValueError('Overlapping resource payload')
        previous = old.get(name)
        if previous and previous['bytes'] == count and previous['sha256'] == item['sha256']:
            if start > position:
                operations.append(('literal', position, start - position))
            operations.append(('file', name, count))
            position = start + count
            reused += count
    if position < target.stat().st_size:
        operations.append(('literal', position, target.stat().st_size - position))
    if not operations or len(operations) > 65536:
        raise ValueError('Recipe exceeds its operation limit')
    target_sha = checksum(target)
    header = (b'TUPD001\0' + struct.pack('<III', kind, target.stat().st_size, len(operations)) +
              identity.encode() + source_version.encode().ljust(24, b'\0') + target_version.encode().ljust(24, b'\0') +
              bytes.fromhex(target_sha))
    temporary = output.with_name(output.name + '.building')
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with temporary.open('wb') as destination, target.open('rb') as source:
            compressor = zlib.compressobj(9)
            def emit(data):
                destination.write(compressor.compress(data))
            emit(header)
            for operation, value, count in operations:
                emit(struct.pack('<BI', 1 if operation == 'literal' else 2, count))
                if operation == 'file':
                    path = value.encode()
                    emit(struct.pack('<H', len(path)) + path + bytes.fromhex(new[value]['sha256']))
                else:
                    source.seek(value)
                    left = count
                    while left:
                        data = source.read(min(65536, left))
                        if not data:
                            raise ValueError('Target changed while generating the patch')
                        emit(data)
                        left -= len(data)
            destination.write(compressor.flush())
        if checksum(target) != target_sha:
            raise ValueError('Target changed while generating the patch')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    report = {'format': 'tupd1', 'platform': platform, 'from': source_version, 'version': target_version,
              'identity': identity, 'kind': kind, 'artifact': suffix[1:], 'file': output.name,
              'bytes': output.stat().st_size, 'sha256': checksum(output), 'targetBytes': target.stat().st_size,
              'targetSha256': target_sha, 'reusedBytes': reused, 'reusedFiles': sum(op[0] == 'file' for op in operations)}
    output.with_name(output.name + '.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['base', 'target', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform', choices=PLATFORMS, required=True)
    parser.add_argument('--from-version', required=True)
    parser.add_argument('--to-version', required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.base, args.target, args.output, args.platform, args.from_version, args.to_version), indent=2))
