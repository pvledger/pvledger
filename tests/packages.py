"""Inspect and extract all nFPM artifacts without installing them on the host."""
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

directory = Path(sys.argv[1]).resolve()
version = re.search(r'project\(PVLedger VERSION ([\d.]+)',
                    (Path(__file__).resolve().parents[1] / 'CMakeLists.txt').read_text())[1]
required = {
    "opt/pvledger/bin/pvledger", "opt/pvledger/share/pvledger/LICENSE",
    "opt/pvledger/share/pvledger/licenses/miniz.txt",
    "opt/pvledger/share/pvledger/db/pvledger.sql",
    "opt/pvledger/share/pvledger/templates/setup.html",
    "opt/pvledger/share/pvledger/js/app.js", "opt/pvledger/share/pvledger/lang/en_US.yaml",
    "opt/pvledger/share/pvledger/themes/default/style.css",
    "opt/pvledger/share/pvledger/plugins/sample/plugin.yaml",
    "etc/init.d/pvledger", "usr/lib/systemd/system/pvledger.service",
}

def output(*args, **kwargs):
    return subprocess.check_output(args, **kwargs)

for suffix in (".deb", ".rpm", ".pkg.tar.zst"):
    matches = list(directory.glob("*" + suffix))
    assert len(matches) == 1, (suffix, matches)
    package = matches[0]
    with tempfile.TemporaryDirectory(prefix="pvledger-package-") as temporary:
        root = Path(temporary)
        if suffix == ".deb":
            members = output("bsdtar", "-tf", str(package)).decode().splitlines()
            control_name = next(name for name in members if name.startswith("control.tar"))
            control = output("bsdtar", "-xOf", str(package), control_name)
            with tarfile.open(fileobj=io.BytesIO(control)) as archive:
                fields = archive.extractfile("./control").read().decode()
                assert "Package: pvledger\n" in fields
                assert f"Version: {version}-" in fields
                assert "Architecture: amd64\n" in fields
                for name in ("postinst", "prerm", "postrm"):
                    assert archive.extractfile("./" + name).read().startswith(b"#!/bin/sh")
                assert b"/etc/init.d/pvledger" in archive.extractfile("./conffiles").read()
            payload = next(name for name in members if name.startswith("data.tar"))
            data = output("bsdtar", "-xOf", str(package), payload)
            subprocess.run(["bsdtar", "-xf", "-", "-C", str(root)], input=data, check=True)
        else:
            subprocess.run(["bsdtar", "-xf", str(package), "-C", str(root)], check=True)
        if suffix == ".rpm" and shutil.which("rpm"):
            info = output("rpm", "-qp", "--queryformat", "%{NAME} %{VERSION} %{ARCH}", str(package)).decode()
            assert info == f"pvledger {version} x86_64"
            hooks = output("rpm", "-qp", "--scripts", str(package)).decode()
            for text in ("useradd", "systemctl stop", "systemctl daemon-reload"):
                assert text in hooks
        elif suffix == ".rpm":
            print("RPM header/script queries skipped: install rpm to enable them (required in CI).")
        files = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()}
        assert required <= files, required - files
        assert not any(path.endswith((".a", ".hpp", ".sqlite", ".db")) for path in files)
        assert not (root / "opt/pvledger/var").exists(), "User state must never be in the payload"
        assert (root / "etc/init.d/pvledger").stat().st_mode & 0o111
        if suffix == ".pkg.tar.zst":
            info = (root / ".PKGINFO").read_text()
            assert "pkgname = pvledger\n" in info and "arch = x86_64\n" in info
            assert f"pkgver = {version}-" in info
            hooks = (root / ".INSTALL").read_text()
            for hook in ("post_install", "post_upgrade", "pre_remove", "post_remove"):
                assert hook + "()" in hooks
            subprocess.run(["sh", "-n", str(root / ".INSTALL")], check=True)
        binary = str(root / "opt/pvledger/bin/pvledger")
        assert output(binary, "--version").decode().strip() == "PVLedger " + version
        assert b"Usage: pvledger" in output(binary, "--help")
        elf = output("readelf", "-l", "-d", binary)
        assert b"INTERP" not in elf and b"(NEEDED)" not in elf
        subprocess.run(["sh", "-n", str(root / "etc/init.d/pvledger")], check=True)
        print(package.name + ": payload, permissions, CLI, static linkage passed")
