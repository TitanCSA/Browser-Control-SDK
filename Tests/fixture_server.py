"""Serve owned local test pages, including a fictitious sign-in. No real accounts."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import json
from urllib.parse import urlsplit, parse_qs


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        route = urlsplit(self.path).path
        content_type = 'text/html; charset=utf-8'
        headers = {}
        if route in ('/', '/fixture.html'):
            body = Path(__file__).with_name('fixture.html').read_bytes()
        elif route == '/login':
            if 'browser_fixture_login=owned-local-session' in self.headers.get('Cookie', ''):
                body = b'<!doctype html><title>Local account signed in</title><h1>Signed in to the local fixture</h1><p id="signed-in">Fictitious local session restored.</p>'
            else:
                body = Path(__file__).with_name('login_fixture.html').read_bytes()
        elif route == '/second':
            body = b'<!doctype html><title>Second local page</title><h1>Second local page</h1><a id="home" href="/">Return home</a>'
        elif route == '/frame':
            body = b'<!doctype html><title>Local frame</title><button id="frame-button" onclick="this.textContent=\'Frame clicked\'">Frame button</button>'
        elif route == '/download':
            body = b'Browser Control SDK owned local download fixture.\n'
            content_type = 'text/plain; charset=utf-8'
            headers['Content-Disposition'] = 'attachment; filename="browser-control-example.txt"'
        elif route == '/health':
            body = b'{"ok":true}'
            content_type = 'application/json'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if urlsplit(self.path).path != '/login':
            self.send_error(404)
            return
        length = int(self.headers.get('Content-Length', '0'))
        if length < 1 or length > 16384:
            self.send_error(400)
            return
        values = parse_qs(self.rfile.read(length).decode('utf-8'))
        if (values.get('username') != ['demo@example.invalid'] or
                values.get('password') != ['owned-local-fake-password']):
            self.send_error(403, 'Only the documented fictitious account is accepted')
            return
        self.send_response(303)
        self.send_header('Location', '/login')
        self.send_header('Set-Cookie', 'browser_fixture_login=owned-local-session; Path=/; Max-Age=3600; HttpOnly; SameSite=Lax')
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, format, *args):
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--ready-file')
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    address = {'url': 'http://127.0.0.1:' + str(server.server_port)}
    if args.ready_file:
        Path(args.ready_file).write_text(json.dumps(address), encoding='utf-8')
    print(json.dumps(address), flush=True)
    server.serve_forever()
