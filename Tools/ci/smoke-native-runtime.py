#!/usr/bin/env python3
"""Check a relocated native package without a game disc, wallet, or payment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


def architecture(path):
    with path.open('rb') as source:
        header = source.read(64)
        if header.startswith(b'\x7fELF') and len(header) >= 20 and header[5] in (1, 2):
            endian = '<' if header[5] == 1 else '>'
            machine = struct.unpack_from(endian + 'H', header, 18)[0]
            if header[4] == 2 and machine == 62: return 'x86_64'
            if header[4] == 2 and machine == 183: return 'arm64'
        elif header.startswith(b'MZ') and len(header) == 64:
            source.seek(struct.unpack_from('<I', header, 60)[0])
            pe = source.read(6)
            if pe[:4] == b'PE\0\0' and len(pe) == 6:
                machine = struct.unpack_from('<H', pe, 4)[0]
                if machine == 0x8664: return 'x86_64'
                if machine == 0xaa64: return 'arm64'
        elif header[:4] in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf') and len(header) >= 8:
            endian = '<' if header[:4] == b'\xcf\xfa\xed\xfe' else '>'
            machine = struct.unpack_from(endian + 'I', header, 4)[0]
            if machine == 0x01000007: return 'x86_64'
            if machine == 0x0100000c: return 'arm64'
    raise ValueError('Unsupported or incomplete native binary: ' + str(path))


def environment(profile, libraries):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('SATS_ARENA_', 'DYLD_')) and
           key not in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'GH_TOKEN', 'GITHUB_TOKEN')}
    # Linux/macOS must resolve packaged RPATHs, without a build-directory override.
    if sys.platform == 'win32':
        dirs = list(dict.fromkeys(str(path.parent) for path in libraries))
        env['PATH'] = os.pathsep.join(dirs + [env.get('PATH', '')])
    for key, folder in {'HOME': profile, 'USERPROFILE': profile,
                        'APPDATA': profile/'appdata', 'LOCALAPPDATA': profile/'localappdata',
                        'XDG_CONFIG_HOME': profile/'config', 'XDG_DATA_HOME': profile/'data',
                        'XDG_CACHE_HOME': profile/'cache'}.items():
        folder.mkdir(parents=True, exist_ok=True)
        env[key] = str(folder)
    return env


def check_wallet(command, env):
    request = {'version': 1, 'backend': 'nwc', 'nwc_uri': 'not-a-valid-nwc-uri'}
    try:
        result = subprocess.run(command, input=json.dumps(request) + '\n', text=True,
                                capture_output=True, env=env, timeout=15)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError('Packaged wallet checker did not exit within 15 seconds') from error
    if result.returncode != 0:
        raise RuntimeError(f'Packaged wallet checker failed to load or exited {result.returncode}: {result.stderr[-2000:]}')
    try:
        response = json.loads(result.stdout)
    except ValueError as error:
        raise RuntimeError('Packaged wallet checker did not return JSON') from error
    # The pinned engine rejects this URI before opening any relay connection.
    if not isinstance(response, dict) or response.get('ok') is not False or response.get('message') != 'invalid NWC connection string':
        raise RuntimeError('Packaged wallet checker did not reject the invalid URI before connection')
    return {'exit_code': result.returncode, 'ok': False, 'message': response['message'],
            'invalid_configuration': True, 'network_connection_attempted': False, 'invoice_requested': False}


def gui_load(binary, profile, env):
    user = profile/'dolphin-user'
    (user/'Config').mkdir(parents=True)
    (user/'Config/Dolphin.ini').write_text('[Interface]\nConfirmStop = False\nPauseOnFocusLost = False\n'
        '[Core]\nCPUThread = True\n[Display]\nRenderToMain = True\nFullscreen = False\n[DSP]\nBackend = Null\n')
    gui_env = dict(env, SATS_ARENA_FAKE_LOCAL='0')
    with (profile/'gui-load.log').open('w+') as log:
        process = subprocess.Popen([str(binary), '-u', str(user)], cwd=binary.parent, env=gui_env,
                                   stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    log.seek(0)
                    raise RuntimeError(f'Packaged game exited {process.returncode} during GUI load: {log.read()[-4000:]}')
                time.sleep(.2)
            if process.poll() is not None:
                raise RuntimeError('Packaged game exited before GUI load completed')
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    return {'started_without_iso': True, 'isolated_profile': True, 'payment_gate_disabled': True,
            'alive_seconds': 8, 'test_process_stopped': True}


def smoke(args):
    paths = {'game': args.binary, 'wallet_checker': args.checker, 'arena_library': args.library}
    if args.slippi_library is not None:
        paths['slippi_library'] = args.slippi_library
    if sys.platform == 'win32':
        # CI has a global CRT installed; require app-local copies for fresh PCs.
        for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
            paths[name] = args.binary.parent/name
    files = {}
    for name, original in paths.items():
        path = original.expanduser().resolve()
        if not path.is_file(): raise ValueError('Missing packaged file: ' + str(path))
        arch = architecture(path)
        if arch != 'x86_64': raise ValueError(f'{name} architecture is {arch}; expected x86_64')
        files[name] = {'path': str(path), 'architecture': arch,
                       'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        paths[name] = path
    with tempfile.TemporaryDirectory(prefix='sats-arena-native-smoke-') as folder:
        profile = Path(folder)
        env = environment(profile, [paths['arena_library'], *([paths['slippi_library']] if 'slippi_library' in paths else [])])
        result = {'ok': True, 'platform': sys.platform, 'files': files,
                  'wallet_checker': check_wallet([str(paths['wallet_checker'])], env), 'payment_operations': False}
        if args.gui_load:
            result['gui_load'] = gui_load(paths['game'], profile, env)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--checker', required=True, type=Path)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--slippi-library', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--gui-load', action='store_true')
    args = parser.parse_args()
    try:
        result = smoke(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        result = {'ok': False, 'error': str(error), 'payment_operations': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
