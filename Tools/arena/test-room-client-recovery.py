#!/usr/bin/env python3
"""Recover native room membership from 500s/timeouts without wallets or re-admission."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time


def run_case(binary, mode, terminal=404):
    config = {'player_count': 2, 'teams': False, 'team_ids': [255] * 4}
    player = {'address': 'player@example.test', 'invoice': 'unchanged-entry', 'verified': True,
              'paid_sats': 100, 'remaining_sats': 100, 'earned_sats': 0, 'pending_sats': 0}
    payment = {'match_id': 'a' * 32, 'phase': 2, 'frozen': False, 'teams': False,
               'mode': 'fake', 'entry_sats': 100, 'invoice_sats': 100, 'sats_per_percent': 1,
               'batch_window_ms': 1000, 'surcharge_bps': 0, 'agreed_sequence': 0,
               'own_next_sequence': [1] * 4, 'kinds': [0] * 4, 'team_ids': [255] * 4,
               'characters': [17] * 4, 'batch_remaining_ms': [0] * 4,
               'players': [dict(player) for _ in range(4)]}
    room = {'config': config, 'code': 'ABCDEF01', 'players': [
        {'slot': 0, 'name': 'Host', 'endpoint': '127.0.0.1:43513', 'ready': False}],
        'match_id': None, 'payment': payment if mode == 'paid' else None}
    admissions, gets, ready_attempts, starts = 0, 0, 0, 0
    early_connections = set()

    class Mock(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def do_GET(self):
            nonlocal gets
            assert self.headers.get('Authorization') == 'Bearer ' + 'f' * 64
            if self.path.startswith('/netplay/matches/'):
                assert self.path.endswith('a' * 32)
                self.respond(200, payment)
                return
            gets += 1
            if mode == 'terminal':
                self.respond(terminal, {'error': 'Membership ended'})
                return
            if gets == 1 or mode == 'stop':
                early_connections.add(self.client_address)
                self.respond(503, {'error': 'Temporary failure'})
                return
            if gets == 2:
                early_connections.add(self.client_address)
                time.sleep(2.4)  # The native request times out at two seconds.
            else:
                if len(room['players']) == 1:
                    room['players'].append({'slot': 1, 'name': 'Guest',
                                            'endpoint': '127.0.0.1:43514', 'ready': True})
            self.respond(200, room)

        def do_POST(self):
            nonlocal admissions, ready_attempts, starts
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            if self.path == '/netplay/rooms':
                admissions += 1
                early_connections.add(self.client_address)
                assert body['config'] == config
                assert body['lan_endpoint'] == '192.168.1.10:53513'
                room.update(build=body['build'], rules=body['rules'])
                self.respond(200, {'slot': 0, 'token': 'f' * 64, 'room': room})
                return
            assert self.headers.get('Authorization') == 'Bearer ' + 'f' * 64
            if body['type'] == 'ready':
                ready_attempts += 1
                if ready_attempts == 1 and mode not in ('terminal', 'stop'):
                    self.respond(500, {'error': 'Temporary ready failure'})
                    return
                room['players'][0]['ready'] = True
            elif body['type'] == 'start':
                starts += 1
                assert gets >= 3, 'Started using stale room state during the outage'
                room['match_id'] = 'a' * 32
                payment['phase'] = 3
            else:
                assert body['type'] == 'leave'
            self.respond(200, room)

        def respond(self, status, body):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass  # Expected when the delayed response arrives after CURL's timeout.

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Mock)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        env = {key: value for key, value in os.environ.items() if not key.startswith('SATS_ARENA_')}
        env['SATS_ARENA_ROOM_SERVER_URL'] = f'http://127.0.0.1:{server.server_port}'
        subprocess.run([str(binary.resolve()), mode], env=env, check=True, timeout=15)
        assert admissions == 1, 'A transient failure created a replacement membership'
        assert len(early_connections) == 1, 'CURL did not reuse the healthy HTTP connection'
        if mode not in ('terminal', 'stop'):
            assert gets >= 3 and ready_attempts == 2 and starts == 1
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[2]
                        / 'build-arena/tests/room-client-recovery-test')
    args = parser.parse_args()
    for mode in ('free', 'paid', 'stop'):
        run_case(args.binary, mode)
    for terminal in (401, 404):
        run_case(args.binary, 'terminal', terminal)
