#!/usr/bin/env python3
"""Check the real online sidebar in an isolated netplay-test client (GTK/X11).

Run inside slippi-arena-build with --pid HOST_NATIVE_PID. This uses visible UI
controls only and never funds entries, changes room rules, or reads wallets.
"""
import argparse
import ast
import os
from pathlib import Path
import subprocess
import shutil
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid', type=int, required=True)
args = parser.parse_args()
profile_root = str(Path.home() / '.local/share/sats-arena/netplay-test/player-')
command = (Path('/proc') / str(args.pid) / 'cmdline').read_bytes().split(b'\0')
assert any(part.decode(errors='replace').startswith(profile_root) for part in command), 'Use an isolated test client'
address = ast.literal_eval(subprocess.check_output([
    'gdbus', 'call', '--session', '--dest', 'org.a11y.Bus', '--object-path',
    '/org/a11y/bus', '--method', 'org.a11y.Bus.GetAddress'], text=True))[0]
os.environ['AT_SPI_BUS_ADDRESS'] = address
import pyatspi


def walk(node):
    yield node
    for child in node:
        yield from walk(child)


xdotool = ['xdotool'] if shutil.which('xdotool') else ['distrobox-host-exec', 'xdotool']

app = next(a for a in pyatspi.Registry.getDesktop(0)
           if a.name == 'dolphin-emu' and a.get_process_id() == args.pid)


def find(role, name):
    return next(n for n in walk(app) if n.getRoleName() == role and n.name == name)


def showing(node):
    return node.getState().contains(pyatspi.STATE_SHOWING)


def click(node):
    assert node.queryAction().doAction(0), 'GTK action failed'
    time.sleep(.4)


hide = find('push button', 'Hide ›')
restore = find('push button', '‹')
room = find('toggle button', 'Room')
frame = find('frame', next(n.name for n in app if n.getRoleName() == 'frame'))
window = subprocess.check_output(xdotool + ['search', '--all', '--pid', str(args.pid),
                                  '--name', 'Faster Melee'], text=True).splitlines()[0]
subprocess.run(xdotool + ['windowactivate', '--sync', window], check=True)
assert showing(hide), 'Start with the online panel open'
bounds = list(frame.queryComponent().getExtents(0))
sidebar = next(n for n in walk(app) if n.getRoleName() == 'scroll pane' and
               showing(n) and n.queryComponent().getExtents(0).width == 256)
height = sidebar.queryComponent().getExtents(0).height


def game_width():
    return max(n.queryComponent().getExtents(0).width for n in walk(app)
               if n.getRoleName() == 'panel' and showing(n) and
               n.queryComponent().getExtents(0).height == height and
               n.queryComponent().getExtents(0).width < bounds[2] - 2)


expanded = game_width()
click(hide)
assert not showing(hide) and showing(restore)
assert restore.queryComponent().getExtents(0).width == 28
assert game_width() == expanded + 228, 'Hidden panel must return 228px to gameplay'
assert list(frame.queryComponent().getExtents(0)) == bounds
click(restore)
assert showing(hide) and not showing(restore)
click(room)
assert not showing(hide)
click(room)
assert showing(hide)
view = find('menu', 'View')
item = next(n for n in walk(app) if n.getRoleName() == 'check menu item' and
            n.name.startswith('Multiplayer panel'))
click(view)
click(item)
assert not showing(hide)
click(view)
click(item)
assert showing(hide)


def shortcut(hold=.12):
    subprocess.run(xdotool + ['keydown', 'Control_L', 'Shift_L', 'm'], check=True)
    time.sleep(hold)
    subprocess.run(xdotool + ['keyup', 'm', 'Shift_L', 'Control_L'], check=True)


for expected in (False, True):
    shortcut(.7 if not expected else .12)
    time.sleep(.4)
    assert showing(hide) == expected, 'Shortcut must toggle exactly once'
click(find('push button', 'FullScr'))
assert not showing(room), 'Fullscreen must hide the toolbar'
for expected in (False, True):
    shortcut(.7 if not expected else .12)
    time.sleep(.4)
    assert showing(hide) == expected, 'Shortcut must survive fullscreen menu removal'
subprocess.run(xdotool + ['keydown', 'Alt_L', 'Return'], check=True)
time.sleep(.15)
subprocess.run(xdotool + ['keyup', 'Return', 'Alt_L'], check=True)
time.sleep(.5)
assert showing(room) and showing(hide)
print('PASS: 256px sidebar, 28px restore tab, 228px gameplay gain, Room toolbar, shortcut and fullscreen')
