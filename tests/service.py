"""Exercise the SysV template against a real server in a disposable directory.

Run as root to verify privilege dropping; no host service is installed or enabled.
"""
import http.client
import json
import os
from pathlib import Path
import pwd
import shutil
import socket
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
template = Path(sys.argv[2]).resolve()
subprocess.run(["sh", "-n", str(template)], check=True)
if os.geteuid() != 0:
    print("Service lifecycle test needs root to verify setpriv; shell syntax passed.")
    sys.exit(77)

account = pwd.getpwnam("nobody")
with tempfile.TemporaryDirectory(prefix="pvledger-service-") as directory:
    root = Path(directory)
    root.chmod(0o755)
    daemon = root / "pvledger"
    shutil.copy2(binary, daemon)
    work = root / "var"
    work.mkdir(mode=0o700)
    os.chown(work, account.pw_uid, account.pw_gid)
    # Install resources alongside the executable: never depend on a developer's home permissions.
    (root / "bin").mkdir()
    daemon = root / "bin/pvledger"
    shutil.move(root / "pvledger", daemon)
    assets = root / "share/pvledger"
    assets.mkdir(parents=True)
    source = Path(__file__).resolve().parents[1]
    for name in ("templates", "js", "lang", "themes"):
        shutil.copytree(source / name, assets / name)
    shutil.copytree(source / "src/plugins", assets / "plugins")
    (assets / "db").mkdir()
    shutil.copy2(source / "db/pvledger.sql", assets / "db/pvledger.sql")
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    config = work / "config.yaml"
    config.write_text(f"""app:
  host: 127.0.0.1
  port: {port}
database:
  engine: sqlite
  sqlite:
    path: data/pvledger.sqlite
paths:
  languages: {assets}/lang
  themes: {assets}/themes
  plugins: {assets}/plugins
""")
    os.chown(config, account.pw_uid, account.pw_gid)
    config.chmod(0o600)
    script = root / "pvledger.init"
    lines = template.read_text().splitlines()
    replacements = {
        "DAEMON": str(daemon), "SERVICE_USER": str(account.pw_uid),
        "SERVICE_GROUP": str(account.pw_gid), "WORKDIR": str(work), "RUNDIR": str(root / "run"),
    }
    script.write_text("\n".join(
        key + "=" + replacements[key] if (key := line.split("=", 1)[0]) in replacements else
        line.replace("/etc/default/pvledger", str(root / "defaults")) for line in lines) + "\n")
    script.chmod(0o755)

    def service(action, expected=0):
        result = subprocess.run([str(script), action], text=True, capture_output=True, timeout=40)
        assert result.returncode == expected, (action, result.returncode, result.stdout, result.stderr)
        return result.stdout

    pidfile = root / "run/pvledger.pid"
    try:
        service("status", 3)
        service("invalid", 2)
        service("start")
        first_pid = int(pidfile.read_text().split()[0])
        assert Path(f"/proc/{first_pid}").stat().st_uid == account.pw_uid
        assert os.getsid(first_pid) == first_pid, "Service must detach from the invoking terminal"
        service("start")
        assert int(pidfile.read_text().split()[0]) == first_pid
        service("status")
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
        connection.request("GET", "/api/v1/system/health")
        assert json.load(connection.getresponse()) == {"status": "setup"}
        connection.close()
        assert (work / "data/pvledger.sqlite").stat().st_uid == account.pw_uid
        original_config = config.read_bytes()
        service("restart")
        assert int(pidfile.read_text().split()[0]) != first_pid
        assert config.read_bytes() == original_config
        service("stop")
        service("stop")
        service("status", 3)
        # A stale pidfile must never signal an unrelated live process.
        with subprocess.Popen(["sleep", "60"]) as other:
            try:
                start_time = Path(f"/proc/{other.pid}/stat").read_text().split()[21]
                pidfile.write_text(f"{other.pid} {start_time}\n")
                service("status", 3)
                service("stop")
                assert other.poll() is None
            finally:
                other.terminate()
        config.write_text("invalid: [")
        service("start", 1)
        assert not pidfile.exists()
    finally:
        service("stop")
print("SysV lifecycle, ownership, stale PID handling, and startup failure passed.")
