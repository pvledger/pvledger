#!/bin/sh
set -eu
# Debian: remove/deconfigure; RPM: 0 on final removal; pacman: installed version.
# Debian and RPM invoke removal hooks during upgrades: keep the live service running.
case "${1:-}" in upgrade|failed-upgrade|1) exit 0 ;; esac
if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
    systemctl stop pvledger.service
    systemctl disable pvledger.service
else
    if [ -x /etc/init.d/pvledger ]; then /etc/init.d/pvledger stop; fi
    # chkconfig needs the init script to still exist during deregistration.
    if command -v update-rc.d >/dev/null 2>&1; then
        update-rc.d -f pvledger remove
    elif command -v chkconfig >/dev/null 2>&1; then
        chkconfig --del pvledger
    fi
fi
