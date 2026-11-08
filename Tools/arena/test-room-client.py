#!/usr/bin/env python3
"""Run native room checks through an HTTP proxy that detects redundant readiness writes."""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', default='http://127.0.0.1:8738')
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[2] / 'build-arena/tests/room-client-test')
    args = parser.parse_args()
    counts = Counter()
    lock = threading.Lock()

    class Proxy(BaseHTTPRequestHandler):
        def do_GET(self):
            self.forward()

        def do_POST(self):
            self.forward()

        def forward(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
            if body and json.loads(body).get('type') == 'ready':
                with lock:
                    counts[self.headers.get('Authorization')] += 1
            request = urllib.request.Request(args.server.rstrip('/') + self.path,
                                             data=body if self.command == 'POST' else None,
                                             method=self.command,
                                             headers={'Content-Type': 'application/json',
                                                      'Authorization': self.headers.get('Authorization', '')})
            try:
                response = urllib.request.urlopen(request, timeout=3)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                output = response.read()
                self.send_response(response.status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(output)))
                self.end_headers()
                self.wfile.write(output)

        def log_message(self, *_args):
            pass

    proxy = ThreadingHTTPServer(('127.0.0.1', 0), Proxy)
    worker = threading.Thread(target=proxy.serve_forever, daemon=True)
    worker.start()
    try:
        env = dict(os.environ, SATS_ARENA_ROOM_SERVER_URL=f'http://127.0.0.1:{proxy.server_port}')
        subprocess.run([str(args.binary)], env=env, check=True, timeout=30)
        assert sorted(counts.values()) == [1, 2], 'Client posted readiness after its slot was already ready'
        print('PASS: ready writes occur only for false-to-true transitions, including roster reset')
    finally:
        proxy.shutdown()
        proxy.server_close()
        worker.join(timeout=3)


if __name__ == '__main__':
    main()
