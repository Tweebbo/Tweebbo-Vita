#!/usr/bin/env python3
"""Prepare complete and incremental release assets locally; never publishes."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

spec = importlib.util.spec_from_file_location('builder', Path(__file__).with_name('make-incremental.py'))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--platform', required=True, choices=builder.PLATFORMS)
parser.add_argument('--target', required=True, type=Path)
parser.add_argument('--version', required=True)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--base', action='append', default=[], help='VERSION=PATH to an immutable older NRO/VPK; up to four')
parser.add_argument('--notes-file', type=Path)
args = parser.parse_args()
kind, identity, suffix = builder.PLATFORMS[args.platform]
repository = 'Tweebbo/TweebboNX' if args.platform == 'switch' else 'Tweebbo/Tweebbo-Vita'
if args.target.suffix != suffix or len(args.base) > 4:
    raise ValueError('Use the matching platform package and at most four bases')
builder.version(args.version)
builder.entries(args.target, args.platform, args.version)
args.output.mkdir(parents=True, exist_ok=True)
target = args.output / ('tweebbo' + suffix)
if target.resolve() != args.target.resolve():
    if target.exists():
        raise ValueError('Output already contains a package; use a new release directory')
    shutil.copyfile(args.target, target)
prefix = f'https://github.com/{repository}/releases/download/v{args.version}/'
manifest = {'schema': 1, 'platform': args.platform, 'version': args.version, 'identity': identity,
            'notes': args.notes_file.read_text() if args.notes_file else 'Optional update. Keep playing and update whenever convenient.',
            'full': {'url': prefix + target.name, 'bytes': target.stat().st_size, 'sha256': builder.checksum(target)},
            'incremental': []}
seen = set()
for value in args.base:
    version, separator, path = value.partition('=')
    if not separator or version in seen or builder.version(version) >= builder.version(args.version):
        raise ValueError('Use unique, older VERSION=PATH bases')
    seen.add(version)
    base = Path(path)
    if base.resolve() == target.resolve():
        raise ValueError('A base must not be the target package')
    patch = args.output / f'tweebbo-from-{version}{suffix}.tupd'
    report = builder.build(base, target, patch, args.platform, version, args.version)
    if report['bytes'] < target.stat().st_size:
        manifest['incremental'].append({'format': 'tupd1', 'from': version,
            suffix[1:]: {'url': prefix + patch.name, 'bytes': report['bytes'], 'sha256': report['sha256']}})
    else:
        patch.unlink()
body = json.dumps(manifest, indent=2) + '\n'
if len(body.encode()) >= 16384:
    raise ValueError('Release manifest exceeds the reader limit')
temporary = args.output / 'latest.json.part'
temporary.write_text(body)
temporary.replace(args.output / 'latest.json')
print(json.dumps({'status': 'PREPARED', 'platform': args.platform, 'version': args.version,
                  'bases': len(manifest['incremental']), 'fullFallbackRetained': True, 'published': False}, indent=2))
