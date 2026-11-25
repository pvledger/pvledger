#!/bin/sh
set -eu
getent group pvledger >/dev/null || groupadd --system pvledger
if ! getent passwd pvledger >/dev/null; then
    useradd --system --gid pvledger --home-dir /opt/pvledger/var --no-create-home --shell /bin/false pvledger
fi
install -d -m 0700 -o pvledger -g pvledger /opt/pvledger/var
# Configuration and SQLite files are created by setup, never shipped as package payload.
if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
fi
# Starting/enabling is an explicit operator action, including after upgrades.
