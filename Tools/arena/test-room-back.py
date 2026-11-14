#!/usr/bin/env python3
"""Check controller cancel clears a free room and allows a fresh create (isolated profile only)."""
import argparse
import ast
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--profile', type=Path, required=True)
args = parser.parse_args()
assert str(args.profile).startswith(str(Path.home() / '.local/share/sats-arena/netplay-test/player-'))
assert str(args.profile).encode() in (Path('/proc') / str(args.pid) / 'cmdline').read_bytes().split(b'\0')
address = ast.literal_eval(subprocess.check_output([
    'gdbus', 'call', '--session', '--dest', 'org.a11y.Bus', '--object-path',
    '/org/a11y/bus', '--method', 'org.a11y.Bus.GetAddress'], text=True))[0]
os.environ['AT_SPI_BUS_ADDRESS'] = address
import pyatspi
app = next(a for a in pyatspi.Registry.getDesktop(0) if a.get_process_id() == args.pid)
repo = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('controller', repo.parent / 'dolphin-controller-mcp/server.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Controller(module.Controller):
    def path(self, port):
        return self.user / 'Pipes/netplay'

controller = Controller(args.profile)

def labels(node=app):
    result = []
    if node.getState().contains(pyatspi.STATE_SHOWING):
        result.append(node.name)
    for child in node:
        result.extend(labels(child))
    return result

def wait_for(predicate):
    for _ in range(150):
        current = labels()
        if predicate(current):
            return current
        time.sleep(.1)
    raise AssertionError(labels())

def tap(button):
    controller.run(1, [{'buttons': [button], 'duration_ms': 150}, {'duration_ms': 200}])

wait_for(lambda names: 'No room' in names)
tap('A')
time.sleep(2)
controller.run(1, [{'main': [.7, 1], 'duration_ms': 450}, {'main': [.5, 0], 'duration_ms': 130},
                   {'duration_ms': 200}, {'buttons': ['A'], 'duration_ms': 150}, {'duration_ms': 200}])
codes = []
for _ in range(2):
    tap('START')
    names = wait_for(lambda names: 'WAITING FOR PLAYERS · 1 / 4 JOINED' in names)
    code = next(name for name in names if re.fullmatch('[A-F0-9]{8}', name or ''))
    assert code not in codes, 'Cancel resumed the old room'
    codes.append(code)
    tap('Z')
    wait_for(lambda names: 'CREATE OR JOIN A ROOM IN MELEE' in names and 'No room' in names and code not in names)
print('PASS: free-room waiting count, controller cancel clears panel, retry creates a fresh room')
