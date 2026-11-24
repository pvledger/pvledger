"""Run the browser-native CSV, QIF and OFX parsers on local fixtures."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import signal
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Event, Thread
import time
from urllib.parse import quote
from urllib.request import Request, urlopen

root = Path(__file__).resolve().parents[1]
finished = Event()
results = []


class Fixtures(SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/result":
            self.send_error(404)
            return
        size = int(self.headers["Content-Length"])
        results.append(self.rfile.read(min(size, 4096)).decode())
        self.send_response(204)
        self.end_headers()
        finished.set()


with ThreadingHTTPServer(("127.0.0.1", 0), partial(Fixtures, directory=root)) as server:
    Thread(target=server.serve_forever, daemon=True).start()
    with TemporaryDirectory() as profile:
        browser = subprocess.Popen([sys.argv[1], "--headless", "--disable-features=Crashpad",
                                    "--disable-background-networking", "--no-first-run", "--disable-dev-shm-usage",
                                    "--remote-debugging-port=0", "--user-data-dir=" + profile,
                                    "about:blank"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            port_file = Path(profile) / "DevToolsActivePort"
            for _ in range(100):
                if port_file.exists() and port_file.read_text().splitlines():
                    break
                assert browser.poll() is None, "Headless browser exited before startup"
                time.sleep(0.1)
            port = port_file.read_text().splitlines()[0]
            target = f"http://127.0.0.1:{server.server_port}/tests/import-formats.html"
            with urlopen(Request(f"http://127.0.0.1:{port}/json/new?" + quote(target, safe=""), method="PUT"), timeout=5):
                pass
            assert finished.wait(20), "Browser did not report a fixture result"
            assert results == ["passed"], results
        finally:
            # Chromium helpers can keep writing the disposable profile after the parent exits.
            try:
                os.killpg(browser.pid, signal.SIGTERM)
                browser.wait(timeout=10)
            finally:
                try:
                    os.killpg(browser.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                browser.wait()
