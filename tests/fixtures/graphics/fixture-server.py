#!/usr/bin/env python3
"""Local graphics fixture server for MatchFrame Multiview testing (NOT a MatchFrame API).

Serves overlay.html and records every *page load* (the per-load id the page POSTs to
/load). Asset requests (css/js) are logged separately and never counted as loads.

  python3 fixture-server.py [--port 8765] [--log loads.jsonl]
  Browser Source URL:  http://127.0.0.1:8765/overlay.html
  GET /stats           -> {"page_loads": N, "load_ids": [...], "last_beat": {...}}
  POST /reset          -> clears the counters

Acceptance use: note page_loads, perform 100+ cuts across the eight camera scenes, and
confirm page_loads is unchanged (only an explicit Apply URL / Refresh may add one).
"""
import argparse
import json
import os
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

STATE = {"page_loads": 0, "load_ids": [], "asset_requests": 0, "last_beat": {}}
LOG = None


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj, indent=1).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/stats":
            return self._json(STATE)
        if url.path.endswith((".css", ".js")):
            STATE["asset_requests"] += 1  # cached or not, assets are never page loads
        return super().do_GET()

    def do_POST(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        lid = (q.get("id") or [""])[0]
        now = time.time()
        if url.path == "/load":
            STATE["page_loads"] += 1
            STATE["load_ids"].append({"id": lid, "t": now})
            if LOG:
                with open(LOG, "a") as f:
                    f.write(json.dumps({"event": "page_load", "id": lid, "t": now}) + "\n")
            print("page load #%d id=%s" % (STATE["page_loads"], lid), flush=True)
            return self._json({"ok": True})
        if url.path == "/beat":
            STATE["last_beat"] = {"id": lid, "t": now}
            return self._json({"ok": True})
        if url.path == "/reset":
            STATE.update(page_loads=0, load_ids=[], asset_requests=0, last_beat={})
            return self._json({"ok": True})
        return self._json({"error": "not found"}, 404)


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--log", default=None)
    a = ap.parse_args()
    LOG = a.log
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print("fixture: http://127.0.0.1:%d/overlay.html  (stats: /stats)" % a.port, flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
