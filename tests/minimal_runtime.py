"""Smoke-test an installed Linux release with only its executable and assets mounted."""
import http.client
import json
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
from tempfile import TemporaryDirectory
import time

prefix = Path(sys.argv[1]).resolve()
with TemporaryDirectory() as directory:
    data = Path(directory)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    (data / "config.yaml").write_text(f"""app:
  host: 127.0.0.1
  port: {port}
database:
  engine: sqlite
  sqlite:
    path: ledger.sqlite
paths:
  languages: /share/pvledger/lang
  themes: /share/pvledger/themes
  plugins: /share/pvledger/plugins
""")
    run = ["bwrap", "--die-with-parent", "--clearenv", "--ro-bind", str(prefix / "bin/pvledger"), "/bin/pvledger",
           "--ro-bind", str(prefix / "share/pvledger"), "/share/pvledger", "--bind", directory, "/data",
           "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--chdir", "/data", "/bin/pvledger"]
    for flag in ("--help", "--version"):
        subprocess.run(run + [flag], check=True)
    app = subprocess.Popen(run + ["-c", "/data/config.yaml"])
    try:
        for _ in range(100):
            assert app.poll() is None, "Server exited during startup"
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
            try:
                connection.request("GET", "/api/v1/system/health")
                assert json.load(connection.getresponse()) == {"status": "setup"}
                break
            except ConnectionRefusedError:
                time.sleep(0.05)
            finally:
                connection.close()
        else:
            raise AssertionError("Server did not become ready")
        with sqlite3.connect(data / "ledger.sqlite") as db:
            assert db.execute("SELECT count(*) FROM pvl_countries").fetchone() == (249,)
    finally:
        app.terminate()
        app.wait(timeout=10)
