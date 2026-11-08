#!/usr/bin/env python3
"""Prove native shutdown flushes the complete money report before leaving.

Uses an isolated mock HTTP server and synthetic tracker packets; no wallets or ledgers.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading


def run_case(binary, reject_first, remote_close=False, rematch=False):
    match_id = 'a' * 32
    config = {'player_count': 4, 'teams': False, 'team_ids': [255] * 4}
    player = {'address': 'player@example.test', 'invoice': '', 'verified': True,
              'paid_sats': 100, 'remaining_sats': 100, 'earned_sats': 0, 'pending_sats': 0}
    payment = {'match_id': match_id, 'phase': 3, 'frozen': False, 'teams': False,
               'mode': 'fake', 'entry_sats': 100, 'invoice_sats': 100, 'sats_per_percent': 1,
               'batch_window_ms': 1000, 'surcharge_bps': 0, 'agreed_sequence': 0,
               'own_next_sequence': [1] * 4, 'kinds': [0] * 4, 'team_ids': [255] * 4,
               'characters': [17] * 4, 'batch_remaining_ms': [0] * 4,
               'players': [dict(player) for _ in range(4)]}
    room = {'config': config, 'code': 'ABCDEF01', 'players': [
        {'slot': i, 'name': 'Player', 'endpoint': f'127.0.0.1:{43413+i}', 'ready': True}
        for i in range(4)], 'match_id': match_id, 'payment': payment}
    events, reports = [], []
    close_attempts = 0

    class Mock(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith('/netplay/matches/'):
                target = self.path.rsplit('/', 1)[1]
                self.respond(200, old_payment if rematch and target == 'a' * 32 and room.get('round') == 2 else payment)
            elif remote_close:
                self.respond(404, {'error': 'Room closed'})
            else:
                self.respond(200, room)

        def do_POST(self):
            nonlocal close_attempts
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            if self.path == '/netplay/rooms':
                assert body['config'] == config
                room.update(build=body['build'], rules=body['rules'])
                self.respond(200, {'slot': 0, 'token': 'f' * 64, 'room': room})
                return
            assert self.headers.get('Authorization') == 'Bearer ' + 'f' * 64
            if body['type'] == 'report':
                events.append('end_attempt')
                report = body['report']
                expected = 'b' * 32 if room.get('round') == 2 else match_id
                assert report['match_id'] == expected
                assert report['manifest'] == {'game_number': room.get('round', 1), 'stage': 31, 'seed': 42,
                    'teams': False, 'kinds': [0] * 4, 'team_ids': [255] * 4, 'characters': [17] * 4}
                assert report['end'] == {'last_sequence': 1, 'winner': 3, 'complete': True}
                assert report['credits'] == [{'sequence': 1, 'frame': 1, 'attacker': 3,
                                              'victim': 2, 'damage_centi': 1250}]
                reports.append(report)
                if reject_first and len(reports) == 1:
                    self.respond(409, {'error': 'Temporary observation rejection'})
                    return
                events.append('end_persisted')
                payment['own_next_sequence'][0] = 2
                if rematch:
                    payment['phase'] = 4
                self.respond(200, payment)
            elif body['type'] == 'next_match' and rematch:
                assert body['match_id'] == match_id and payment['phase'] == 4
                old_payment.update(json.loads(json.dumps(payment)))
                room.update(round=2, match_id=None, match_complete=False)
                payment.update(match_id='b' * 32, phase=1, own_next_sequence=[1] * 4)
                for player in payment['players']:
                    player.update(address='', invoice='fresh-entry', verified=False, paid_sats=0, remaining_sats=0)
                for player in room['players']:
                    player['ready'] = False
                self.respond(200, room)
            elif body['type'] == 'set_address' and rematch:
                assert self.path.endswith('b' * 32)
                for player in payment['players']:
                    player.update(address=body['address'], verified=True, paid_sats=100, remaining_sats=100)
                for player in room['players']:
                    player['ready'] = True
                payment['phase'] = 3
                room['match_id'] = 'b' * 32
                self.respond(200, payment)
            elif body['type'] == 'ready' and rematch:
                self.respond(200, room)
            elif body['type'] == 'close' and rematch:
                close_attempts += 1
                self.respond(409 if close_attempts == 1 else 200,
                             {'error': 'Temporary close rejection'} if close_attempts == 1 else room)
            elif body['type'] == 'leave':
                events.append('leave')
                self.respond(200, payment)
            else:
                self.respond(409, {'error': 'Unexpected test request'})

        def respond(self, status, body):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Mock)
    old_payment = {}
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        env = {key: value for key, value in os.environ.items() if not key.startswith('SATS_ARENA_')}
        env['SATS_ARENA_ROOM_SERVER_URL'] = f'http://127.0.0.1:{server.server_port}'
        flag = ['--rematch'] if rematch else ['--remote-close'] if remote_close else []
        subprocess.run([str(binary.resolve()), *flag], env=env, check=True, timeout=20)
        expected = ['end_attempt', 'end_persisted'] * 2 if rematch else (
            ['end_attempt', 'end_attempt', 'end_persisted', 'leave'] if reject_first else
            ['end_attempt', 'end_persisted'] + ([] if remote_close else ['leave']))
        assert events == expected, events
        assert len(reports) == (2 if reject_first or rematch else 1)
        if rematch:
            assert close_attempts == 2
            print('PASS: same room, fresh unpaid match/sequence, two complete reports, failed Close safely retried')
            return
        if remote_close:
            print('PASS: final report still reaches its durable endpoint after room closure')
            return
        print('PASS: complete credit/result persisted before Leave' + (' after one rejected request' if reject_first else ''))
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[2]
                        / 'build-arena/tests/room-client-finalize-test')
    args = parser.parse_args()
    run_case(args.binary, False)
    run_case(args.binary, True)
    run_case(args.binary, False, remote_close=True)
    run_case(args.binary, False, rematch=True)
