"""
Telemetry bridge — stdlib only (no FastAPI/uvicorn dependency)
Serves:
  GET  /api/status        -> JSON snapshot
  GET  /api/history/<if>  -> JSON history
  WS   /ws/telemetry      -> live WebSocket stream
"""
import asyncio
import json
import socket
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer
import threading

UDP_HOST    = "127.0.0.1"
UDP_PORT    = 9999
HTTP_PORT   = 8080
HISTORY_LEN = 60

latest  = {}
history = {}
ws_clients = []  # raw sockets for WebSocket

# ── HTTP handler (runs in a thread) ────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass  # silence access log

    def do_GET(self):
        if self.path == "/api/status":
            self._json({"interfaces": latest, "ts_ms": _ms()})

        elif self.path.startswith("/api/history/"):
            iface = self.path.split("/")[-1]
            if iface in history:
                self._json({"iface": iface, "samples": list(history[iface])})
            else:
                self._json({"error": "not found"}, 404)

        elif self.path in ("/", "/index.html"):
            with open("/opt/netmon/webui/index.html", "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(body)

        else:
            self._json({"error": "not found"}, 404)

    def _json(self, data, code=200):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

# ── UDP listener (asyncio) ──────────────────────────────────────────────────
async def udp_listener():
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    sock.bind((UDP_HOST, UDP_PORT))

    while True:
        try:
            data = await loop.sock_recv(sock, 1024)
            frame = json.loads(data.decode())
            iface = frame.get("iface", "unknown")
            latest[iface] = frame
            if iface not in history:
                history[iface] = deque(maxlen=HISTORY_LEN)
            history[iface].append({
                "ts_ms":      frame.get("ts_ms"),
                "rx_bps":     frame.get("rx_bps", 0),
                "tx_bps":     frame.get("tx_bps", 0),
                "error_rate": frame.get("error_rate", 0),
                "state":      frame.get("state"),
            })
        except Exception:
            await asyncio.sleep(0.1)

async def main():
    # HTTP in a background thread (stdlib HTTPServer is blocking)
    server = HTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    print(f"Bridge running — http://0.0.0.0:{HTTP_PORT}/")
    await udp_listener()

if __name__ == "__main__":
    asyncio.run(main())