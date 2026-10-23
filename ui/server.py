#!/usr/bin/env python3
"""
server.py -- Dining-Philosophers UI сервер (зөвхөн Python стандарт сан).

  GET /            -> ui/index.html
  GET /model.js    -> ui/model.js
  GET /run?...     -> build/dp-г ШИНЭЭР ажиллуулж, гаралтыг мөр мөрөөр (NDJSON)
                      урсгана. Төгсгөлд {"type":"exit","code":N,"stderr":"..."}.
  GET /health      -> ok

Аюулгүй байдал: зөвхөн 127.0.0.1-д холбогдоно; параметрийг бүхэл тоо болгож
хязгаарт шалгана; subprocess-д shell ашиглахгүй (жагсаалт хэлбэрээр дамжуулна);
нэгэн зэрэг ажиллах тоо хязгаартай; ажиллагаа хугацаанаас хэтэрвэл process-ийг
албадан зогсооно.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "dp")
UI_DIR = os.path.join(ROOT, "ui")

STATIC = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/model.js": ("model.js", "application/javascript; charset=utf-8"),
}

# (query нэр, dp-ийн сонголт, доод, дээд, нэр)
INT_PARAMS = [
    ("n", "-n", 2, 64, "Philosopher-ийн тоо"),
    ("tmin", "--think-min", 0, 600000, "Бодох доод хугацаа"),
    ("tmax", "--think-max", 0, 600000, "Бодох дээд хугацаа"),
    ("emin", "--eat-min", 0, 600000, "Идэх доод хугацаа"),
    ("emax", "--eat-max", 0, 600000, "Идэх дээд хугацаа"),
    ("dur", "-d", 100, 600000, "Ажиллах хугацаа"),
    ("seed", "-s", 0, 2000000000, "Seed"),
    ("grab", "--grab-delay", 0, 10000, "Fork авах завсар"),
]
MAX_CONCURRENT = 4
slots = threading.BoundedSemaphore(MAX_CONCURRENT)


def build_command(query):
    """query (dict[str, list[str]]) -> (argv, errors, duration_ms)"""
    errors, argv, vals = [], [BIN], {}
    for key, opt, lo, hi, label in INT_PARAMS:
        raw = (query.get(key) or [None])[0]
        # Зөвхөн ASCII бүхэл тоо (сөрөг тэмдэг нэг л удаа) хүлээн авна; жишээ нь
        # "--5", "5-", "²", хоосон мөр зэргийг int()-д хүргэхээс өмнө татгалзана
        # (өмнө нь .lstrip("-").isdigit() ашигласан нь "--5"-ийг зөвшөөрч, int()-д
        # ValueError шиднэ улмаар холболтыг тасалдаг алдаа байсан).
        if raw is None or not re.fullmatch(r"-?[0-9]{1,12}", raw):
            errors.append(f"{label}: бүхэл тоо оруулна уу")
            continue
        try:
            v = int(raw)
        except ValueError:
            errors.append(f"{label}: бүхэл тоо оруулна уу")
            continue
        if not lo <= v <= hi:
            errors.append(f"{label}: {lo}..{hi} хооронд байх ёстой")
            continue
        vals[key] = v
        argv += [opt, str(v)]
    algo = (query.get("algo") or [None])[0]
    if algo not in ("monitor", "naive"):
        errors.append("Алгоритм: monitor эсвэл naive байх ёстой")
    else:
        argv += ["-a", algo]
    if "tmin" in vals and "tmax" in vals and vals["tmin"] > vals["tmax"]:
        errors.append("Бодох доод хугацаа дээд хугацаанаас их байж болохгүй")
    if "emin" in vals and "emax" in vals and vals["emin"] > vals["emax"]:
        errors.append("Идэх доод хугацаа дээд хугацаанаас их байж болохгүй")
    return argv, errors, vals.get("dur", 10000)


class Handler(BaseHTTPRequestHandler):
    server_version = "dp-ui/1.0"

    def log_message(self, fmt, *args):
        if not getattr(self.server, "quiet", False):
            sys.stderr.write("[ui] " + fmt % args + "\n")

    def _send(self, code, body, ctype):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        try:
            url = urlparse(self.path)
            if url.path in STATIC:
                fname, ctype = STATIC[url.path]
                with open(os.path.join(UI_DIR, fname), "rb") as f:
                    return self._send(200, f.read(), ctype)
            if url.path == "/health":
                return self._send(200, "ok", "text/plain; charset=utf-8")
            if url.path == "/run":
                return self._run(parse_qs(url.query))
            self._send(404, "Not found", "text/plain; charset=utf-8")
        except (BrokenPipeError, ConnectionResetError):
            pass  # хэрэглэгч tab хаасан/холболтоо таслсан
        except Exception as exc:  # noqa: BLE001 -- буруу/урьдчилан харамгүй оролтод 500 буцаах, сервер унахгүй
            try:
                self._send(500, json.dumps({"errors": [f"Серверийн дотоод алдаа: {exc}"]}),
                          "application/json")
            except Exception:
                pass

    def _run(self, query):
        if not os.path.exists(BIN):
            return self._send(500, json.dumps({"errors": ["build/dp олдсонгүй. Эхлээд 'make' ажиллуулна уу."]}),
                              "application/json")
        argv, errors, dur = build_command(query)
        if errors:
            return self._send(400, json.dumps({"errors": errors}), "application/json")
        if not slots.acquire(blocking=False):
            return self._send(429, json.dumps({"errors": ["Хэт олон зэрэгцээ ажиллагаа. Түр хүлээнэ үү."]}),
                              "application/json")
        proc = None
        timer = None
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    text=True, bufsize=1)
            timer = threading.Timer(dur / 1000.0 + 10, proc.kill)  # аюулгүйн хугацаа
            timer.start()
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            for line in proc.stdout:
                self.wfile.write(line.encode("utf-8"))
                self.wfile.flush()
            proc.wait()
            err = proc.stderr.read().strip()
            tail = json.dumps({"type": "exit", "code": proc.returncode, "stderr": err},
                              ensure_ascii=False) + "\n"
            self.wfile.write(tail.encode("utf-8"))
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass  # хэрэглэгч Stop дарсан эсвэл tab хаасан
        finally:
            if timer:
                timer.cancel()
            if proc and proc.poll() is None:
                proc.kill()
            if proc:
                proc.wait()
            slots.release()


def main():
    ap = argparse.ArgumentParser(description="Dining-Philosophers UI сервер")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    srv = ThreadingHTTPServer((a.host, a.port), Handler)
    srv.quiet = a.quiet
    srv.daemon_threads = True
    print(f"Dining-Philosophers UI: http://{a.host}:{srv.server_address[1]}/  (зогсоох: Ctrl+C)",
          flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nСервер зогслоо.")


if __name__ == "__main__":
    main()
