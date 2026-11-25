#!/bin/sh
set -eu
case "${1:-}" in upgrade|failed-upgrade|1) exit 0 ;; esac
if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
fi
# Keep the account and /opt/pvledger/var, even on purge, to preserve financial data.
