#!/usr/bin/env python3
"""Prepare or launch two to four isolated Dolphin room-netplay clients.

Clients never inherit wallet credentials; optional room payments belong to the server.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
ROOT = Path.home() / '.local/share/sats-arena/netplay-test'
NAMES = ('Alice', 'Bob', 'Carol', 'Dave')


def prepare(profile, index):
    (profile / 'Config').mkdir(parents=True, exist_ok=True)
    (profile / 'Pipes').mkdir(exist_ok=True)
    (profile / 'Replays').mkdir(exist_ok=True)
    fifo = profile / 'Pipes/netplay'
    if not fifo.exists():
        os.mkfifo(fifo, 0o600)
    (profile / 'Config/Dolphin.ini').write_text(
        '[Core]\nSIDevice0 = 6\nSIDevice1 = 0\nSIDevice2 = 0\nSIDevice3 = 0\n'
        'CPUThread = True\nEnableCheats = True\nSlippiSaveReplays = True\n'
        f'SlippiReplayDir = {profile / "Replays"}\n'
        '[Interface]\nConfirmStop = False\nPauseOnFocusLost = False\n'
        f'MainWindowPosX = {(index % 2) * 650}\nMainWindowPosY = {(index // 2) * 600}\n'
        'MainWindowWidth = 640\nMainWindowHeight = 600\n'
        '[Input]\nBackgroundInput = True\n'
        '[Display]\nRenderToMain = True\nFullscreen = False\n'
        'RenderWindowWidth = 640\nRenderWindowHeight = 480\n'
        '[DSP]\nBackend = Null\n')
    bindings = {
        **{f'Buttons/{b.title() if b == "START" else b}': f'`Button {b}`'
           for b in ['A', 'B', 'X', 'Y', 'Z', 'START']},
        **{f'D-Pad/{d}': f'`Button D_{d.upper()}`' for d in ['Up', 'Down', 'Left', 'Right']},
        **{f'{stick}/{direction}': f'`Axis {axis} {dimension} {sign}`'
           for stick, axis in [('Main Stick', 'MAIN'), ('C-Stick', 'C')]
           for direction, dimension, sign in [('Up', 'Y', '+'), ('Down', 'Y', '-'), ('Left', 'X', '-'), ('Right', 'X', '+')]},
        'Main Stick/Dead Zone': '0', 'C-Stick/Dead Zone': '0',
        **{f'Triggers/{b}': f'`Button {b}`' for b in ['L', 'R']},
        **{f'Triggers/{b}-Analog': f'`Axis {b} +`' for b in ['L', 'R']},
    }
    (profile / 'Config/GCPadNew.ini').write_text('[GCPad1]\nDevice = Pipe/0/netplay\n' + ''.join(f'{k} = {v}\n' for k, v in bindings.items()))
    (profile / 'Config/Logger.ini').write_text('[Options]\nWriteToFile = True\nWriteToConsole = True\nVerbosity = 4\n[Logs]\nSLIPPI_ONLINE = True\nSLIPPI = True\nBOOT = True\nVIDEO = True\n')


def child_environment(descriptor=None, server=None, index=0):
    if index not in range(len(NAMES)):
        raise ValueError('Player index must be 0 through 3')
    env = {k: v for k, v in os.environ.items() if not k.startswith('SATS_ARENA_')}
    env.update(GDK_BACKEND='x11', SATS_ARENA_FAKE_LOCAL='0')
    if server:
        env.update(SATS_ARENA_NETPLAY_ROOMS='1', SATS_ARENA_ROOM_SERVER_URL=server,
                   SATS_ARENA_ROOM_ENDPOINT=f'127.0.0.1:{43113 + index}',
                   SATS_ARENA_ROOM_NAME=NAMES[index])
    else:
        env['SATS_ARENA_NETPLAY_FILE'] = str(descriptor)
    return env


def running():
    found = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            args = (process / 'cmdline').read_bytes().split(b'\0')
            if (process / 'exe').resolve().name.removesuffix(' (deleted)') == 'dolphin-emu' and any(str(ROOT / f'player-{i}').encode() in args for i in range(1, len(NAMES) + 1)):
                found.append(int(process.name))
        except (OSError, PermissionError):
            pass
    return found


def self_test():
    with tempfile.TemporaryDirectory() as folder:
        endpoints = set()
        for index, name in enumerate(NAMES):
            profile = Path(folder) / f'player-{index + 1}'
            prepare(profile, index)
            assert (profile / 'Pipes/netplay').is_fifo()
            assert 'Pipe/0/netplay' in (profile / 'Config/GCPadNew.ini').read_text()
            assert not (profile / 'GameSettings').exists()
            rooms = child_environment(server='http://127.0.0.1:8738', index=index)
            assert rooms['SATS_ARENA_ROOM_NAME'] == name
            assert rooms['SATS_ARENA_ROOM_ENDPOINT'] == f'127.0.0.1:{43113 + index}'
            assert rooms['SATS_ARENA_NETPLAY_ROOMS'] == '1'
            assert 'SATS_ARENA_NETPLAY_FILE' not in rooms
            endpoints.add(rooms['SATS_ARENA_ROOM_ENDPOINT'])
        assert len(endpoints) == 4
        assert child_environment(Path('/tmp/descriptor.json'))['SATS_ARENA_FAKE_LOCAL'] == '0'
        try:
            child_environment(server='http://127.0.0.1:8738', index=4)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid player index accepted')
    print('PASS: four isolated profiles, unique endpoints, pipe controls, no local wallet credentials')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--players', type=int, choices=range(2, 5), default=2)
    parser.add_argument('--launch', nargs='+', metavar='PLAYER_JSON', type=Path)
    parser.add_argument('--rooms', help='Launch in-game Create/Join against this room-server URL')
    parser.add_argument('--binary', type=Path, default=REPO / 'build-arena/Binaries/dolphin-emu')
    parser.add_argument('--stop', action='store_true')
    parser.add_argument('--status', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    active = running()
    if args.stop:
        for pid in active:
            os.kill(pid, signal.SIGTERM)
        print(json.dumps({'stopped_pids': active}))
        return
    if args.status:
        print(json.dumps({'root': str(ROOT), 'native_pids': active}))
        return
    if active:
        parser.error('Isolated clients are already running; stop them before preparing profiles')
    if args.rooms and args.launch:
        parser.error('Choose --rooms or --launch')
    if args.launch and len(args.launch) != args.players:
        parser.error('Supply one descriptor per player selected by --players')
    if args.launch or args.rooms:
        if not args.binary.is_file() or (args.launch and any(not p.is_file() for p in args.launch)):
            parser.error('Build the binary and supply existing descriptor files for all players')
        # Extract only the ISO path; never copy the normal profile or wallet configuration.
        settings = Path.home() / '.config/Slippi Launcher/Settings'
        iso = json.loads(settings.read_text()).get('settings', {}).get('isoPath')
        if not iso or not Path(iso).is_file():
            parser.error('Select an existing Melee ISO in the normal launcher first')
    ROOT.mkdir(parents=True, exist_ok=True, mode=0o700)
    result = []
    for index in range(args.players):
        profile = ROOT / f'player-{index + 1}'
        prepare(profile, index)
        row = {'profile': str(profile), 'fifo': str(profile / 'Pipes/netplay')}
        if args.launch or args.rooms:
            log = ROOT / f'player-{index + 1}.log'
            with log.open('w') as stream:
                process = subprocess.Popen(['distrobox', 'enter', '-n', 'slippi-arena-build', '--no-tty', '--', str(args.binary.resolve()), '-b', '-u', str(profile), '-e', iso, '--slippi-spectator-port', str(52441 + index)], env=dict(child_environment(args.launch[index].resolve() if args.launch else None, args.rooms, index), XDG_CONFIG_HOME=str(profile / "xdg-config")), stdout=stream, stderr=stream, start_new_session=True)
            row.update(wrapper_pid=process.pid, log=str(log))
        result.append(row)
    (ROOT / 'session.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
