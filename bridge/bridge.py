"""
rpi-net-monitor — Python telemetry bridge
==========================================
Receives JSON frames from the C++ daemon over UDP (127.0.0.1:9999),
buffers the latest state per interface, and exposes:

  GET  /api/status          — snapshot of all interfaces (JSON)
  GET  /api/status/{iface}  — snapshot of one interface  (JSON)
  WS   /ws/telemetry        — live stream, one JSON frame per poll cycle

Run:
    python3 -m uvicorn bridge:app --host 0.0.0.0 --port 8080
"""

import asyncio
import json
import logging
import socket
import time
from collections import deque
from typing import Dict, Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
UDP_HOST       = "127.0.0.1"
UDP_PORT       = 9999
UDP_BUFSIZE    = 1024
HISTORY_LEN    = 60   # keep last 60 samples per interface (~1 min at 1 Hz)

# ---------------------------------------------------------------------------
# Shared state
# ---------------------------------------------------------------------------
# latest snapshot per interface
latest: Dict[str, Dict[str, Any]] = {}
# rolling history per interface (for charts)
history: Dict[str, deque] = {}
# connected WebSocket clients
ws_clients: list[WebSocket] = []

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("bridge")

# ---------------------------------------------------------------------------
# FastAPI app
# ---------------------------------------------------------------------------
app = FastAPI(title="rpi-net-monitor bridge", version="1.0.0")


@app.get("/api/status")
async def get_status():
    """Return the latest telemetry snapshot for all interfaces."""
    return {"interfaces": latest, "timestamp_ms": _now_ms()}


@app.get("/api/status/{iface}")
async def get_iface_status(iface: str):
    """Return the latest telemetry for a single interface."""
    if iface not in latest:
        raise HTTPException(status_code=404, detail=f"Interface '{iface}' not found")
    return latest[iface]


@app.get("/api/history/{iface}")
async def get_iface_history(iface: str):
    """Return the rolling history (up to HISTORY_LEN samples) for one interface."""
    if iface not in history:
        raise HTTPException(status_code=404, detail=f"Interface '{iface}' not found")
    return {"iface": iface, "samples": list(history[iface])}


@app.websocket("/ws/telemetry")
async def ws_telemetry(websocket: WebSocket):
    """WebSocket endpoint — pushes every incoming UDP frame to clients."""
    await websocket.accept()
    ws_clients.append(websocket)
    log.info("WS client connected, total=%d", len(ws_clients))
    try:
        while True:
            # Keep connection alive; data is pushed from udp_listener task
            await asyncio.sleep(30)
            await websocket.send_json({"type": "ping"})
    except WebSocketDisconnect:
        pass
    finally:
        ws_clients.remove(websocket)
        log.info("WS client disconnected, total=%d", len(ws_clients))


# Serve the web dashboard from /webui (mounted at startup)
app.mount("/", StaticFiles(directory="/opt/netmon/webui", html=True), name="ui")


# ---------------------------------------------------------------------------
# UDP listener — runs as a background asyncio task
# ---------------------------------------------------------------------------
async def udp_listener():
    """Receive UDP datagrams from the C++ daemon and fan out to WS clients."""
    loop = asyncio.get_running_loop()

    # Non-blocking UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    sock.bind((UDP_HOST, UDP_PORT))
    log.info("UDP listener bound to %s:%d", UDP_HOST, UDP_PORT)

    while True:
        try:
            data = await loop.sock_recv(sock, UDP_BUFSIZE)
        except Exception as exc:
            log.warning("UDP recv error: %s", exc)
            await asyncio.sleep(0.1)
            continue

        try:
            frame: Dict[str, Any] = json.loads(data.decode())
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            log.warning("Bad UDP frame: %s", exc)
            continue

        iface = frame.get("iface", "unknown")
        frame["bridge_rx_ms"] = _now_ms()

        # Update latest state
        latest[iface] = frame

        # Append to history
        if iface not in history:
            history[iface] = deque(maxlen=HISTORY_LEN)
        history[iface].append({
            "ts_ms":      frame.get("ts_ms"),
            "rx_bps":     frame.get("rx_bps", 0),
            "tx_bps":     frame.get("tx_bps", 0),
            "error_rate": frame.get("error_rate", 0),
            "state":      frame.get("state"),
        })

        # Fan-out to WebSocket clients
        dead = []
        for ws in ws_clients:
            try:
                await ws.send_text(data.decode())
            except Exception:
                dead.append(ws)
        for ws in dead:
            ws_clients.remove(ws)


@app.on_event("startup")
async def startup():
    asyncio.create_task(udp_listener())
    log.info("Bridge started — dashboard at http://0.0.0.0:8080/")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _now_ms() -> int:
    return int(time.time() * 1000)
