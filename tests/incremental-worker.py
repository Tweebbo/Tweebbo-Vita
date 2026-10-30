#!/usr/bin/env python3
"""Run both production updater branches against isolated real HTTP fixtures."""
import hashlib
import http.server
import json
import os
import struct
import subprocess
import tempfile
import threading
import zlib
from pathlib import Path

project = Path(__file__).resolve().parents[1]
transport = project / 'source/nx/conn.c'
if not transport.exists():
    transport = next((project.parent / name / 'source/nx/conn.c' for name in ['switch', 'TweebboNX', 'Tweebbo-Switch']
                      if (project.parent / name / 'source/nx/conn.c').exists()), None)
    if transport is None:
        raise RuntimeError('Run this shared worker suite from the companion TweebboNX checkout; it supplies the POSIX host transport for both device branches.')
build = project / 'build/incremental-worker-tests'
build.mkdir(parents=True, exist_ok=True)
sha = lambda data: hashlib.sha256(data).hexdigest()
results = []
scenarios = ['legacy', 'small-success', 'small-interrupted', 'small-base-missing', 'small-base-damaged',
             'small-header', 'small-checksum', 'small-trailing', 'unknown-base', 'untrusted-patch',
             'bad-manifest-hash', 'bad-manifest-identity', 'bad-full-url', 'bad-version', 'prerelease',
             'wrong-full-hash', 'write-fail', 'rename-fail', 'rename-no-backup', 'small-rename-retry']

for device in [2, 3]:
    binary = build / ('worker-' + str(device))
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation',
                    '-Wno-unused-variable', '-DTWEEBBO_HOST', '-DNO_TLS', '-DTWEEBBO_DEVICE=' + str(device),
                    '-DAPP_VERSION="1.4.3"', '-I' + str(project / 'source'),
                    str(project / 'tests/incremental-worker.c'), str(project / 'source/update.c'),
                    str(project / 'source/incremental.c'), str(project / 'source/ws.c'),
                    str(transport), '-lmbedcrypto', '-ljansson', '-lz', '-lpthread',
                    '-Wl,--wrap=rename', '-Wl,--wrap=fwrite', '-o', str(binary)], check=True)
    suffix, kind, identity = ('nro', 3, 'TWEEBBO-SWITCH__') if device == 2 else ('vpk', 4, 'TWEEBBO-VITA____')
    for scenario in scenarios:
        with tempfile.TemporaryDirectory(prefix='portable-updater-') as temporary:
            root = Path(temporary)
            (root / 'update').mkdir()
            (root / 'resources').mkdir()
            (root / 'userdata').mkdir()
            old = b'previous application/download'
            for name in ['previous.bin', 'target.nro', 'update/tweebbo.vpk']:
                (root / name).write_bytes(old)
            for name in ['saved-session.txt', 'userdata/session.txt']:
                (root / name).write_bytes(b'isolated-test-character-do-not-change')
            data, head, tail = os.urandom(128 * 1024), os.urandom(8192), b'updated metadata' * 17
            payload = head + data + tail
            (root / 'payload.bin').write_bytes(payload)
            (root / 'resources/unchanged.asset').write_bytes(data)
            if scenario == 'small-base-missing':
                (root / 'resources/unchanged.asset').unlink()
            if scenario == 'small-base-damaged':
                (root / 'resources/unchanged.asset').write_bytes(b'X' * len(data))
            header = (b'TUPD001\0' + struct.pack('<III', kind, len(payload), 3) + identity.encode() +
                      b'1.4.3'.ljust(24, b'\0') + b'1.4.4'.ljust(24, b'\0') + bytes.fromhex(sha(payload)))
            recipe = (header + struct.pack('<BI', 1, len(head)) + head + struct.pack('<BIH', 2, len(data), 15) +
                      b'unchanged.asset' + bytes.fromhex(sha(data)) + struct.pack('<BI', 1, len(tail)) + tail)
            if scenario == 'small-header':
                recipe = b'BAD' + recipe[3:]
            patch = zlib.compress(recipe, 9) + (b'extra bytes' if scenario == 'small-trailing' else b'')
            state = {'full': 0, 'patch': 0, 'ranges': [], 'manifest': 0}
            files = {}

            class Handler(http.server.BaseHTTPRequestHandler):
                def log_message(self, *args):
                    pass

                def do_GET(self):
                    if self.path not in files:
                        self.send_error(404)
                        return
                    blob = files[self.path]
                    if self.path == '/full.' + suffix:
                        state['full'] += 1
                    if self.path == '/patch.tupd':
                        state['patch'] += 1
                        state['ranges'].append(self.headers.get('Range'))
                    if self.path == '/latest.json':
                        state['manifest'] += 1
                    start = int(self.headers.get('Range', 'bytes=0-').split('=')[1].split('-')[0])
                    self.send_response(206 if start else 200)
                    self.send_header('Content-Length', str(len(blob) - start))
                    if start:
                        self.send_header('Content-Range', f'bytes {start}-{len(blob)-1}/{len(blob)}')
                    self.end_headers()
                    if self.path == '/patch.tupd' and scenario == 'small-interrupted' and state['patch'] == 1:
                        self.wfile.write(blob[:800])
                        self.wfile.flush()
                        self.close_connection = True
                    else:
                        self.wfile.write(blob[start:])

            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            origin = 'http://127.0.0.1:' + str(server.server_port)
            manifest = {'version': '1.4.4', 'identity': identity,
                        'full': {'url': origin + '/full.' + suffix, 'bytes': len(payload), 'sha256': sha(payload)},
                        'incremental': [{'format': 'tupd1', 'from': '1.4.3', suffix:
                                         {'url': origin + '/patch.tupd', 'bytes': len(patch), 'sha256': sha(patch)}}]}
            if scenario == 'unknown-base':
                manifest['incremental'][0]['from'] = '1.4.2'
            if scenario in ['write-fail', 'rename-fail', 'rename-no-backup']:
                manifest['incremental'][0]['from'] = '1.4.2'
            if scenario == 'untrusted-patch':
                manifest['incremental'][0][suffix]['url'] = 'https://untrusted.example/app.tupd'
            if scenario == 'bad-manifest-identity':
                manifest['identity'] = 'ANOTHER-APP______'
            if scenario == 'small-checksum':
                manifest['incremental'][0][suffix]['sha256'] = '0' * 64
            body = json.dumps(manifest).encode()
            assets = [{'name': 'tweebbo.' + suffix, 'size': len(payload), 'digest': 'sha256:' + sha(payload),
                       'browser_download_url': origin + '/full.' + suffix}]
            if scenario != 'legacy':
                assets += [{'name': 'latest.json', 'size': len(body), 'digest': 'sha256:' + ('0' * 64 if scenario == 'bad-manifest-hash' else sha(body)),
                            'browser_download_url': origin + '/latest.json'},
                           {'name': 'patch.tupd', 'size': len(patch), 'digest': 'sha256:' + manifest['incremental'][0][suffix]['sha256'],
                            'browser_download_url': origin + '/patch.tupd'}]
            if scenario == 'wrong-full-hash':
                assets[0]['digest'] = 'sha256:' + '0' * 64
            if scenario == 'bad-full-url':
                assets[0]['browser_download_url'] = 'https://untrusted.example/other.nro'
            release = {'tag_name': 'v1.4.4' if scenario != 'bad-version' else 'vgarbage',
                       'body': 'Optional update; keep playing and update later.', 'prerelease': scenario == 'prerelease', 'assets': assets}
            files.update({'/release': json.dumps(release).encode(), '/latest.json': body,
                          '/patch.tupd': patch, '/full.' + suffix: payload})
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            env = {**os.environ, 'TWEEBBO_UPDATE_MANIFEST': origin + '/release',
                   'TWEEBBO_UPDATE_RESOURCES': str(root / 'resources')}
            if device == 2:
                env['TWEEBBO_UPDATE_TARGET'] = 'target.nro'
            try:
                child = subprocess.Popen([str(binary), scenario], cwd=root, env=env,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                lines = []
                while True:
                    line = child.stdout.readline()
                    if not line:
                        break
                    lines.append(line.strip())
                    if line.startswith('WAIT_FULL'):
                        assert state['full'] == 0, (scenario, state)
                        assert (root / 'target.nro').read_bytes() == old
                        child.stdin.write('\n'); child.stdin.flush()
                    if line.startswith('WAIT_RETRY'):
                        before_retry = dict(state)
                        child.stdin.write('\n'); child.stdin.flush()
                code = child.wait(timeout=45)
                assert code == 0, (device, scenario, lines, child.stderr.read())
                if scenario == 'small-interrupted':
                    assert state['patch'] == 2 and any(value and not value.startswith('bytes=0-') for value in state['ranges'])
                if scenario == 'small-success':
                    assert state['full'] == 0 and state['patch'] == 1
                if scenario in ['rename-fail', 'small-rename-retry']:
                    assert state['full'] == before_retry['full'] and state['patch'] == before_retry['patch']
                results.append({'device': device, 'scenario': scenario, 'status': 'PASS', 'requests': state})
            finally:
                server.shutdown();server.server_close();thread.join()
            print('PASS', suffix, scenario, flush=True)

(build / 'report.json').write_text(json.dumps({'status': 'PASS', 'cases': results, 'count': len(results)}, indent=2) + '\n')
print('PASS portable production updater:', len(results), 'real HTTP, resume, integrity, explicit fallback and atomic replacement cases')
