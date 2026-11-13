#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A development server for testing on a console over the local network.

  GET  /<file>          a build from the dist folder (the .cia a QR code points FBI at)
  POST /log?name=<n>    a log the console uploads; saved as logs/<time>-<n>.log

Usage: devserver.py <dist dir> <logs dir> [port]   (local network only: it has no authentication)
"""
import http.server, os, re, sys, time, urllib.parse

dist, logs = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
port = int(sys.argv[3]) if len(sys.argv) > 3 else 8099
os.makedirs(dist, exist_ok=True); os.makedirs(logs, exist_ok=True)

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=dist, **kwargs)

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path != "/log":
            self.send_error(404); return
        name = re.sub(r"[^A-Za-z0-9_.-]", "_", urllib.parse.parse_qs(url.query).get("name", ["log"])[0])[:60]
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":      # the 3DS's http service sends chunks
            body = b""
            while True:
                size = int(self.rfile.readline().split(b";")[0].strip() or b"0", 16)
                if size == 0:
                    self.rfile.readline(); break
                body += self.rfile.read(size); self.rfile.readline()
                if len(body) > 8 << 20: break
        else:
            body = self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 8 << 20))
        path = os.path.join(logs, time.strftime("%Y%m%d-%H%M%S") + "-" + name + ".log")
        with open(path, "wb") as out: out.write(body)
        print(f"log {path} {len(body)} bytes from {self.client_address[0]}", flush=True)
        self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers(); self.wfile.write(b"ok")

http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
