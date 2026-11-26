#!/usr/bin/env python3
"""智慧籃子 Web 與 SQLite 收集器。預設使用電腦端模擬器。"""

import argparse
import base64
import hmac
import ipaddress
import json
import os
from pathlib import Path
import queue
import re
import sqlite3
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import Request, urlopen

from dashboard_db import Archive

ROOT = Path(__file__).resolve().parents[1]
DEMO = "demo"
SIM_HARDWARE = {name: True for name in ("oled", "nfc", "tof0", "tof1", "hx711", "hx711cal", "storage")}
COMMANDS = {"tap A", "tap B", "tap C", "button", "put 150", "take", "tick 3", "tick 16"}
LEAVE_ITEM = ["tap A", "button", "put 150", "tick 3", "tap A", "tick 16"]
SCENES = {
    "empty": [],
    "pending": LEAVE_ITEM,
    "trash": [*LEAVE_ITEM, "confirm trash A"],
    "example": [*LEAVE_ITEM, "confirm trash A", "tap B", "button", "take", "tick 3", "tap B"],
}
ASSETS = {
    "/": ("index.html", "text/html"),
    "/developer": ("index.html", "text/html"),
    "/styles.css": ("styles.css", "text/css"),
    "/app.js": ("app.js", "application/javascript"),
}
UID_PATTERN = r"[A-Za-z0-9]{1,32}"
LIVE_INTERVAL = 0.25
LIVE_TIMEOUT = 2.5


class UsbDevice:
    def __init__(self, path):
        import serial

        self.lock = threading.Lock()
        self.port = serial.Serial(baudrate=115200, timeout=0.25, write_timeout=1)
        self.port.port = path
        self.port.rts = False

    def request(self, path, method):
        url = urlsplit(path)
        if method == "GET" and url.path == "/api/telemetry":
            command = "telemetry"
        elif method == "POST" and url.path == "/api/calibrate":
            command = "calibrate"
        elif method == "POST" and url.path == "/api/lid":
            closed = query(url).get("closed")
            if closed not in {"0", "1"}:
                raise ValueError("請選擇蓋子開或關")
            command = "lid closed" if closed == "1" else "lid open"
        elif method == "POST" and url.path == "/api/confirm":
            uid, kind = confirmation(query(url))
            command = f"{kind} {uid}"
        elif method == "POST" and url.path == "/api/config" and query(url).get("key") == "dist_mm":
            command = f"distance {distance_setting(query(url))}"
        else:
            raise ValueError("USB 不支援這項操作")
        with self.lock:
            try:
                if not self.port.is_open:
                    self.port.open()
                token = uuid.uuid4().hex[:8]
                prefix = f"BASKET {token} ".encode()
                self.port.write(f"{token} {command}\n".encode())
                deadline = time.monotonic() + 3
                line = bytearray()
                while time.monotonic() < deadline:
                    line.extend(self.port.read_until(size=131072))
                    if len(line) >= 131072:
                        raise OSError("USB 回應過長")
                    if line.endswith(b"\n"):
                        if line.startswith(prefix):
                            return json.loads(line[len(prefix):])
                        line.clear()
                raise TimeoutError("ESP32 USB 回應逾時")
            except OSError:
                self.port.close()
                raise


class Simulator:
    def __init__(self, archive):
        self.archive = archive
        self.lock = threading.RLock()
        self.process = None
        _, previous = archive.latest(DEMO)
        if previous:
            self.start(previous)
        else:
            self.scene("example")

    def close(self):
        if not self.process:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()
        self.process = None

    @staticmethod
    def read(process, output):
        for line in process.stdout:
            output.put(line)
        output.put(None)

    def start(self, previous=None):
        self.close()
        self.boot = uuid.uuid4().hex
        self.output = queue.Queue()
        self.process = subprocess.Popen(
            [str(ROOT / ".pio/build/native/program"), "--dashboard"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8",
        )
        threading.Thread(target=self.read, args=(self.process, self.output), daemon=True).start()
        inputs = previous["inputs"] if previous else [0, 300, 300, 0, 1]
        storage = previous["storage"] if previous else ""
        header = " ".join(map(str, [*inputs, len(storage.encode("utf-8"))]))
        self.process.stdin.write(f"{header}\n{storage}")
        self.last_tick = time.monotonic()

    def exchange(self, commands):
        self.process.stdin.write("\n".join([*commands, "json"]) + "\n")
        self.process.stdin.flush()
        lines = []
        deadline = time.monotonic() + 4
        while True:
            try:
                line = self.output.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty as exc:
                raise RuntimeError("模擬器回應逾時") from exc
            if line is None:
                raise RuntimeError("模擬器已停止")
            if line.startswith('{"state":'):
                snapshot = json.loads(line)
                snapshot.update(bootId=self.boot, hardware=SIM_HARDWARE)
                self.archive.ingest(DEMO, snapshot)
                return "".join(lines)
            lines.append(line)

    def sync(self, commands=()):
        elapsed = int((time.monotonic() - self.last_tick) * 20) / 20
        ticks = [f"tick {elapsed:.2f}"] if elapsed > 0 else []
        self.last_tick += elapsed
        return self.exchange([*ticks, *commands])

    def scene(self, name):
        if name not in SCENES:
            raise ValueError("找不到這個示範情境")
        self.start()
        self.exchange(SCENES[name])


class Devices:
    def __init__(self, archive, endpoints):
        self.archive = archive
        self.sources = endpoints
        self.usb = {name: UsbDevice(urlsplit(base).path) for name, base in endpoints.items()
                    if urlsplit(base).scheme == "serial"}
        self.sim = Simulator(archive) if not endpoints else None
        self.stop = threading.Event()
        self.errors = {}
        self.frames = {}
        self.frame_ready = threading.Condition()
        self.last_archive = {}
        self.workers = []
        for device in endpoints or (DEMO,):
            thread = threading.Thread(target=self.collect, args=(device,), daemon=True)
            self.workers.append(thread)
            thread.start()

    def request(self, device, path, method="GET"):
        if device in self.usb:
            return self.usb[device].request(path, method)
        url = self.sources[device] + path
        with urlopen(Request(url, method=method), timeout=2) as response:
            return json.load(response)

    def poll(self, device):
        snapshot = self.request(device, "/api/telemetry")
        frame = self.frames.get(device)
        previous = frame[1] if frame else self.archive.latest(device)[1]
        if previous and snapshot["deviceId"] != previous["deviceId"]:
            raise ValueError("裝置識別碼已變更，請用新的裝置名稱收集，以保留原本的帳戶與紀錄。")
        snapshot["bootId"] = f'{snapshot["deviceId"]}:{snapshot["bootId"]}'
        observed = time.time()
        now = time.monotonic()
        if now - self.last_archive.get(device, -1) >= 1:
            self.archive.ingest(device, snapshot, observed)
            self.last_archive[device] = now
        with self.frame_ready:
            self.frames[device] = (observed, snapshot)
            self.frame_ready.notify_all()

    def next_frame(self, device, after):
        with self.frame_ready:
            self.frame_ready.wait_for(
                lambda: self.stop.is_set() or self.frames.get(device, (None,))[0] != after,
                timeout=0.75,
            )
            return self.frames.get(device)

    def collect(self, device):
        while not self.stop.is_set():
            started = time.monotonic()
            try:
                if device == DEMO:
                    with self.sim.lock:
                        self.sim.sync()
                else:
                    self.poll(device)
                self.errors.pop(device, None)
            except (OSError, ValueError, TypeError, RuntimeError, KeyError, sqlite3.Error) as exc:
                self.errors[device] = str(exc)
            interval = 1 if device == DEMO or device in self.errors else LIVE_INTERVAL
            self.stop.wait(max(0, interval - (time.monotonic() - started)))

    def action(self, device, params):
        if device == DEMO:
            with self.sim.lock:
                if "scene" in params:
                    self.sim.scene(params["scene"])
                    return
                if "command" in params:
                    command = params["command"]
                    if command not in COMMANDS:
                        raise ValueError("不支援的模擬操作")
                else:
                    uid, kind = confirmation(params)
                    command = f"confirm {kind} {uid}"
                if "沒有待確認的物品" in self.sim.sync([command]):
                    raise ValueError("這筆待確認物品已處理，請更新頁面。")
            return
        observed, snapshot = self.archive.latest(device)
        if device in self.errors or observed is None or time.time() - observed >= 5:
            raise ValueError("裝置目前未連線，請等狀態更新後重試。")
        if params.get("control") == "distance":
            if not snapshot.get("signals", {}).get("tofTest"):
                raise ValueError("此操作只開放給測距測試版")
            path = "/api/config?" + urlencode({"key": "dist_mm", "value": distance_setting(params)})
        elif params.get("control") in {"lid", "calibrate"}:
            if not snapshot.get("manualLid"):
                raise ValueError("此韌體使用實體開關")
            control = params["control"]
            path = "/api/calibrate" if control == "calibrate" else "/api/lid?" + urlencode({"closed": params.get("closed", "")})
        else:
            uid, kind = confirmation(params)
            path = "/api/confirm?" + urlencode({"owner": uid, "kind": kind})
        result = self.request(device, path, "POST")
        if not result["ok"]:
            raise ValueError(result.get("error", "裝置無法完成這項操作，請更新頁面。"))

    def close(self):
        self.stop.set()
        for worker in self.workers:
            worker.join()
        for usb in self.usb.values():
            usb.port.close()
        if self.sim:
            with self.sim.lock:
                self.sim.close()


def confirmation(params):
    uid, kind = params.get("owner", ""), params.get("kind", "")
    if not re.fullmatch(UID_PATTERN, uid) or kind not in {"trash", "mine"}:
        raise ValueError("卡號或確認類型不正確")
    return uid, kind


def distance_setting(params):
    value = params.get("value", "")
    if not re.fullmatch(r"[0-9]{1,4}", value) or not 1 <= int(value) <= 8189:
        raise ValueError("測距門檻須為 1–8189 mm 的正整數")
    return int(value)


class Handler(BaseHTTPRequestHandler):
    def serve(self, body, content_type, status=200):
        self.send_response(status)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; object-src 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def reply(self, value, status=200):
        self.serve(json.dumps(value, ensure_ascii=False).encode(), "application/json", status)

    def authorized(self):
        password = self.server.password
        if not password:
            host = self.headers.get("Host", "").split(":", 1)[0].lower()
            if host in {"127.0.0.1", "localhost", self.server.tailscale_host}:
                return True
            self.reply({"error": "僅接受本機連線"}, 403)
            return False
        expected = b"Basic " + base64.b64encode(f"basket:{password}".encode())
        if hmac.compare_digest(self.headers.get("Authorization", "").encode(), expected):
            return True
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="Smart Basket"')
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def selected(self, params):
        labels = list(self.server.devices.sources) or [DEMO]
        key = params.get("device", labels[0])
        if key not in labels:
            raise ValueError("找不到指定的裝置")
        return key, labels

    def stream_live(self, device):
        devices = self.server.devices
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-store, no-transform")
        self.end_headers()
        after = None
        was_online = None
        try:
            while not devices.stop.is_set():
                frame = devices.next_frame(device, after)
                online = frame is not None and time.time() - frame[0] < LIVE_TIMEOUT
                new_frame = frame is not None and frame[0] != after
                if new_frame:
                    after, snapshot = frame
                if online and new_frame:
                    state = snapshot["state"]
                    payload = {
                        "online": True, "observed": after, "boot": snapshot["bootId"],
                        "signals": snapshot.get("signals", {}),
                        "lidClosed": state["sensors"]["lidClosed"],
                    }
                    self.wfile.write(b"data: " + json.dumps(payload, separators=(",", ":")).encode() + b"\n\n")
                elif not online and was_online is not False:
                    self.wfile.write(b'data: {"online":false}\n\n')
                else:
                    self.wfile.write(b": keepalive\n\n")
                self.wfile.flush()
                was_online = online
        except OSError:
            pass

    def do_GET(self):
        if not self.authorized():
            return
        url = urlsplit(self.path)
        if url.path in ASSETS:
            name, content_type = ASSETS[url.path]
            return self.serve((ROOT / "web" / name).read_bytes(), content_type)
        if url.path == "/api/live":
            try:
                device, _ = self.selected(query(url))
            except ValueError as exc:
                return self.reply({"error": str(exc)}, 400)
            if device == DEMO:
                return self.reply({"error": "即時串流僅提供給實體裝置"}, 404)
            return self.stream_live(device)
        if url.path != "/api/dashboard":
            return self.reply({"error": "找不到頁面"}, 404)
        params = query(url)
        try:
            device, labels = self.selected(params)
            category = params.get("filter", "all")
            if category not in {"all", "rides", "money", "device"}:
                raise ValueError("不支援的紀錄類型")
            before = int(params.get("before", "0"))
            if not 0 <= before < 2**63:
                raise ValueError("頁碼無效")
            value = self.server.archive.view(device, params.get("card"), category, before)
            value["device"] = device
            value["devices"] = [{"id": key, "name": "展示籃 01" if key == DEMO else key} for key in labels]
            value["mode"] = "demo" if device == DEMO else "live"
            value["online"] = value["observed"] is not None and time.time() - value["observed"] < 5
            value["error"] = self.server.devices.errors.get(device)
            self.reply(value)
        except ValueError as exc:
            self.reply({"error": str(exc)}, 400)
        except sqlite3.Error:
            self.reply({"error": "SQLite 暫時無法讀取"}, 503)

    def do_POST(self):
        if not self.authorized():
            return
        origin = self.headers.get("Origin")
        if origin and urlsplit(origin).netloc != self.headers.get("Host"):
            return self.reply({"error": "來源不符"}, 403)
        url = urlsplit(self.path)
        if url.path == "/api/card-label":
            try:
                device, _ = self.selected(query(url))
                length = int(self.headers.get("Content-Length", 0))
                if not 1 <= length <= 1024 or self.headers.get_content_type() != "application/json":
                    raise ValueError("姓名資料格式不正確")
                body = json.loads(self.rfile.read(length))
                if not isinstance(body, dict):
                    raise ValueError("姓名資料格式不正確")
                uid, label = body.get("uid"), body.get("label")
                if not isinstance(uid, str) or not re.fullmatch(UID_PATTERN, uid):
                    raise ValueError("卡號不正確")
                if not isinstance(label, str) or not 1 <= len(label.strip()) <= 40 or not label.strip().isprintable():
                    raise ValueError("請輸入 1–40 字的學生姓名或暱稱")
                self.server.archive.set_card_label(device, uid, label.strip())
                return self.reply({"ok": True})
            except ValueError as exc:
                return self.reply({"error": str(exc)}, 400)
            except sqlite3.Error:
                return self.reply({"error": "姓名暫時無法儲存，請重試"}, 503)
        if url.path != "/api/action":
            return self.reply({"error": "找不到操作"}, 404)
        params = query(url)
        try:
            device, _ = self.selected(params)
            self.server.devices.action(device, params)
            self.reply({"ok": True})
        except ValueError as exc:
            self.reply({"error": str(exc)}, 400)
        except (OSError, RuntimeError, sqlite3.Error):
            self.reply({"error": "裝置或資料庫暫時無法確認結果，請查看最新狀態。"}, 503)

    def log_message(self, fmt, *_args):
        # Requests carry card identifiers in query parameters. Keep them out of HTTP logs.
        pass


def query(url):
    return {key: values[0] for key, values in parse_qs(url.query).items()}


def configured_devices(parser, entries):
    endpoints = {}
    for entry in entries:
        name, separator, base = entry.partition("=")
        url = urlsplit(base)
        serial_url = url.scheme == "serial" and not url.netloc and url.path.startswith("/dev/")
        http_url = (url.scheme in {"http", "https"} and url.hostname and not url.username and
                    not url.password and url.path in {"", "/"})
        if (not separator or not name.strip() or name in endpoints or name == DEMO or
                not (serial_url or http_url) or url.query or url.fragment):
            parser.error("--device 請使用唯一名稱與 HTTP 或 USB 網址，例如 車籃01=serial:///dev/cu.usbserial-1130")
        endpoints[name] = base.rstrip("/")
    return endpoints


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--tailscale-host", type=str.lower, help="Tailscale Serve 的完整 .ts.net 主機名")
    parser.add_argument("--db", type=Path, default=ROOT / "data" / "basket.sqlite3")
    parser.add_argument("--device", action="append", default=[], metavar="NAME=URL")
    args = parser.parse_args()
    endpoints = configured_devices(parser, args.device)
    password = os.environ.get("BASKET_WEB_PASSWORD")
    loopback = ipaddress.IPv4Address(args.host).is_loopback
    if args.tailscale_host and (not loopback or not re.fullmatch(
            r"[a-z0-9-]+\.[a-z0-9-]+\.ts\.net", args.tailscale_host)):
        parser.error("--tailscale-host 僅限本機監聽及完整的 .ts.net 主機名。")
    if not loopback and not password:
        parser.error("開放區域網路存取時，請設定 BASKET_WEB_PASSWORD。")
    if not endpoints:
        subprocess.run(["pio", "run", "-e", "native"], cwd=ROOT, check=True)
    archive = Archive(args.db)
    with ThreadingHTTPServer((args.host, args.port), Handler) as server:
        server.archive = archive
        server.password = password
        server.tailscale_host = args.tailscale_host
        server.devices = Devices(archive, endpoints)
        print(f"智慧籃子 Web · http://{args.host}:{server.server_port}", flush=True)
        print(f"SQLite · {archive.path}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.devices.close()


if __name__ == "__main__":
    main()
