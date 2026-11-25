# PVLedger

Manage your finances privately

PVLedger is a self-hosted ledger for personal accounts, balance history, income, expenses, debts, and taxes. The C++17 application serves its browser interface and JSON API with Oat++ and stores data locally in SQLite.

Copyright (c) 2026 Vadim Prishlyak. PVLedger is available under AGPL-3.0-only or a separate commercial license. Under the AGPL, operators of a modified network service must offer its corresponding source to users who interact with it remotely. For a proprietary modification, contact Vadim Prishlyak to obtain commercial terms. [License](LICENSE) · [Commercial licensing](mailto:pvadimr@pvadimr.com)

## Build and run

The supported release target is Linux x86-64. Install a C++17 compiler with static C/C++ runtimes, binutils, CMake 3.18+, Git, pkg-config, and the SQLite, OpenSSL and JsonCpp development packages. The release needs the SQLite and libcrypto static archives. CMake uses an installed JsonCpp archive or builds JsonCpp 1.9.5. Python 3.8+ runs the HTTP integration suite. CTest also exercises the browser file parsers when it finds a Chromium-based browser. CMake uses installed Oat++ 1.3.0 and yaml-cpp 0.8 if available, or fetches the pinned release tags. It also builds miniz 3.0.2 for spreadsheet packaging.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pvledger -c src/config/config.yaml
```

Open `http://127.0.0.1:8080` and choose **Set up a fresh instance** to set preferences and create the first administrator, or **Restore from a backup** to recover an existing ledger and its users. The example configuration creates a fresh database in `src/config/data`. To bootstrap a missing configuration, pass a new YAML filename in an existing writable directory. PVLedger uses port 8080, its distributed assets, and `data/pvledger.sqlite` relative to that directory, then writes the configuration when setup is submitted. To select a different database or port, provide a configuration file before starting. Setup listens on loopback and closes when either setup path succeeds. Restart after setup to apply any non-loopback binding in an existing configuration.

The default static build verifies the final ELF has no interpreter or shared-library dependencies. Supply a static library prefix through `CMAKE_PREFIX_PATH` if needed. Use `-DPVLEDGER_STATIC=OFF` for a dynamically linked development build.

Use `sudo cmake --install build --component PVLedger` to install the executable and assets under `/opt/pvledger` by default. Set `CMAKE_INSTALL_PREFIX` at configure time for a custom installation. Immutable resources, including templates, scripts, and the initial SQL, are installed under `share/pvledger`. The `PVLedger` component excludes development files belonging to build dependencies.

## Native packages and services

Install [nFPM](https://nfpm.goreleaser.com/docs/install/) (validated with 2.49.0) on the build machine. Packaging requires the static Linux x86-64 build and the `/opt/pvledger` prefix; nFPM is not needed to run the app. The [nFPM configuration template](packaging/nfpm.yaml.in) supplies package contents, metadata, dependencies and lifecycle hooks.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON -DCMAKE_INSTALL_PREFIX=/opt/pvledger
cmake --build build --parallel
cmake --build build --target packages
```

Artifacts appear in `build/packages/`. Select `package-deb`, `package-rpm`, or `package-archlinux` to build just one format. Use `-DPVLEDGER_NFPM=/path/to/nfpm` if it is not on `PATH`, and `-DPVLEDGER_PACKAGE_RELEASE=2` to increment the packaging revision without changing the application version. Package versions come from the CMake project version. Targets stage the install inside the build directory and do not require root or install anything on the build host.

| Systems | Package | Install command (substitute the generated filename) |
| --- | --- | --- |
| Debian / Ubuntu | `.deb` | `sudo apt install ./pvledger_0.1.1-1_amd64.deb` |
| CentOS / Rocky Linux / Red Hat / compatible RPM systems | `.rpm` | `sudo dnf install ./pvledger-0.1.1-1.x86_64.rpm` |
| Arch Linux / Artix / Manjaro | `.pkg.tar.zst` | `sudo pacman -U ./pvledger-0.1.1-1-x86_64.pkg.tar.zst` |

Packages install `/opt/pvledger/bin/pvledger`, the resources under `/opt/pvledger/share/pvledger`, and service templates under `/opt/pvledger/share/doc/PVLedger`. They also install `/etc/init.d/pvledger` and `/usr/lib/systemd/system/pvledger.service` as service-manager integration files. Installation creates a system user/group named `pvledger` and a private, writable `/opt/pvledger/var` directory. The first run uses `/opt/pvledger/var/config.yaml` and `/opt/pvledger/var/data/pvledger.sqlite`; setup creates the configuration if it is missing. The service account cannot modify the executable or bundled assets.

On systemd hosts:

```sh
sudo systemctl enable --now pvledger.service
sudo systemctl status pvledger.service
journalctl -u pvledger.service
```

On SysV init hosts:

```sh
sudo /etc/init.d/pvledger start
sudo /etc/init.d/pvledger status
sudo /etc/init.d/pvledger restart
sudo /etc/init.d/pvledger stop
```

Enable boot startup using the host's SysV tools, for example `sudo update-rc.d pvledger defaults` on Debian, or `sudo chkconfig --add pvledger` followed by `sudo chkconfig pvledger on` on compatible RPM systems. The SysV script uses `setpriv` and `flock` from util-linux, drops privileges before running the app, writes logs to `/opt/pvledger/var/pvledger.log`, and keeps PID/lock files under root-owned `/run/pvledger`. Arrange log rotation for long-running SysV installations. Optional root-owned `/etc/default/pvledger` shell settings can override the variables at the top of the template. Configuration reload is a restart. Use one service manager per instance. Artix installations using OpenRC, runit, s6 or dinit need a service definition for that manager; the SysV template can still be invoked manually.

For a manual CMake installation, run `sudo sh packaging/scripts/postinstall.sh` from the source checkout to create the service account and data directory (for the default prefix), then copy the generated SysV template from `build/packaging/pvledger.init` to `/etc/init.d/pvledger` with mode `0755`, or install the generated systemd unit. Custom prefixes require adjusting the account home/data directory and service paths.

Package installation does not start or enable the service automatically. Open `http://127.0.0.1:8080/setup` after starting, or use an SSH tunnel to that address on a remote server. Back up the ledger before upgrades, then restart the service explicitly after installing the new package. Existing configuration and SQLite files are not package payload and survive upgrades, removal and Debian purge; removal stops the service and retains the account and data. Delete retained financial data only when you intentionally want to discard it.

Validate artifacts with `python3 tests/packages.py build/packages` (requires `bsdtar` and `readelf`; RPM metadata checks also use `rpm` when installed). `sudo python3 tests/service.py build/pvledger build/packaging/pvledger.init` tests the real SysV lifecycle and privilege dropping in a temporary directory without registering a host service. CI runs these checks and the existing application/minimal-runtime suites, then uploads all three packages.

## Configuration and access

Relative paths resolve from the configuration file's directory.

| YAML key | Meaning |
| --- | --- |
| `app.host` | IPv4 bind address, default `127.0.0.1` |
| `app.port` | TCP port from 1 to 65535 |
| `app.secure_cookie` | Enable the Secure session cookie for HTTPS deployments |
| `database.engine` | `sqlite` |
| `database.sqlite.path` | SQLite file |
| `paths.languages` | Directory of locale YAML files including `en_US.yaml` |
| `paths.plugins` | Directory of local plugin directories |
| `paths.themes` | Directory of theme directories including `default/style.css` |

Administrators can see all records and manage users, plugins, and settings. Read/Write users can edit finances linked to their own accounts and shared reference data, subject to administrator-only edits for countries and currencies. Viewers can read their own finances and shared reference data. Access is based on `id_role`. Sessions expire after 30 minutes of inactivity and on server restart. New passwords use salted PBKDF2-HMAC-SHA256. A successful legacy SHA-256 login upgrades its stored digest.

The English locale includes all application strings. The German, Kazakh, and Russian packs cover the main screens and navigation and fall back to English for omitted keys. Copy a language file to add a locale, set `meta.locale` to its filename stem, and restart. A theme is a directory containing `style.css` under the configured themes path. Assign language, currency, and theme from Settings.

The JSON API uses `/api/v1`. Income and balance history are available to browse and filter. Income status is shown as its stored integer. Adding an actual expense debits its current account. Adding actual income credits it. Set a starting balance first and use the account currency for expenses. Each change to an existing current or savings balance stores the resulting balance with the current UTC time. Multiple changes in one second have distinct history IDs. Editing or deleting a posted entry does not adjust the account again. Dates use UTC epoch seconds and the dashboard groups totals by currency. See the [API contract](docs/api/README.md) and [OpenAPI document](docs/api/openapi.yaml).

## Import, export and reports

Open **Data → Import** to load a local CSV, QIF, OFX or QFX file. Choose the file encoding, then map the destination current account, expense category, currency, date format and expense sign. For CSV, select the delimiter, header and source columns. The preview shows each mapped expense, row errors and possible duplicates. Select the rows to save and confirm once. Download the row details to retain the review. Imports support up to 500 records in a 512 KiB file.

The first import destination is actual expenses. The wizard accepts bank, cash and credit-card transaction QIF sections, and a single bank or credit-card statement in OFX 1.x SGML or OFX 2.x XML, including QFX files with those structures. A positive expense is stored in the explicitly selected currency. Source currency conflicts, unsupported splits, investment records, corrections and unrepresentable dates require review. For QIF-style two-digit years, 70–99 select 1970–1999 and 00–69 select 2000–2069. OFX timezone offsets are applied before storage. Preview flags matching account, currency, name, timestamp and amount, as well as repeated source identifiers within the file. The existing schema has no persistent external transaction ID. Confirmation posts each selected expense in the same transaction as its balance update and history. If any row fails, the entire batch rolls back.

**Data → Export** offers UTF-8 CSV, OpenDocument (.ods) and Excel (.xlsx) for accounts, histories, financial entries and reference data. Choose the filters and columns, review the count and sample, then download. Relationship columns include IDs and selectable readable labels. Dates use UTC epoch seconds. Workbooks preserve column order and store amounts as numeric cells and user-entered text as literal strings. Integers with more than 15 digits use text cells to preserve their exact value. QIF, OFX and QFX are also available for actual transactions from one account and currency. A download holds up to 5,000 rows and 8 MiB. Larger datasets can be split with filters.

**Reports → Cash flow estimates** shows daily income, expenses and net cash flow across all accessible current accounts or for one selected account. Through the end of today (UTC), it counts actual entries. From tomorrow it counts outstanding planned entries (status Planned, or an unset planned-expense status). Each row identifies its basis and each currency has its own table.

**Reports → Spending over time** charts daily spending and lists the same totals in a table. Select a current account, expense currency, or date range to narrow the report. Each currency has its own series. A day appears when an authorized actual expense has a recorded date and amount, using UTC day boundaries.

**Expenses by category** groups actual expenses within each currency. Category totals containing negative values or lacking a positive sum are shown in the table alone. **Balance history** retains every current or savings account snapshot, ordering equal timestamps by snapshot ID. Lines connect observations and the table lists their recorded times. Filters apply to the signed-in user's authorized records.

## Optional Docker deployment

Docker Compose publishes container port 8080 on the host at `127.0.0.1:8080`. The `PVLEDGER_CONTAINER_BIND` setting lets the setup wizard and the default service listen on the container network while retaining their local Host header checks. Check that host port 8080 is available.

```sh
docker compose up --build -d
docker compose logs -f pvledger
docker compose down
```

Open `http://127.0.0.1:8080/setup`. A named volume stores configuration and SQLite files under `/var/lib/pvledger`. The image runs as the unprivileged `pvledger` user. If you use a bind mount, give that user access to its directory and the files it contains. When changing the volume's configuration, keep the host bind address at `127.0.0.1` for local access. For remote access, place the app behind an HTTPS reverse proxy and enable `app.secure_cookie`.

## Backups and upgrades

In **Administration → Settings**, choose **Download database backup** to save a consistent SQLite snapshot while PVLedger is running. It contains all accounts, users, password hashes, settings, plugin registrations and history. Store it privately. Keep the deployment YAML and any custom plugin, theme and language files separately.

To recover, choose **Restore from a backup** in Settings or on the initial setup screen and select the SQLite file. Settings asks for confirmation before replacing the live ledger. PVLedger checks the file, supported schema, foreign keys and administrator account before committing the replacement. After success, everyone signs in again using credentials from the backup. Both browser operations support files up to 64 MiB.

For larger databases, use SQLite's online backup command or stop PVLedger before copying the database.

```sh
sqlite3 /path/to/pvledger.sqlite '.backup /path/to/pvledger.backup'
sqlite3 /path/to/pvledger.backup 'PRAGMA integrity_check; PRAGMA foreign_key_check;'
```

For an offline restore, stop the application and point its configuration at a verified copy of the backup. Keep the old file until the restored instance starts and its records have been checked. Automatic upgrades create a sibling SQLite backup before changing the schema. A v0.6 database with unlinked history snapshots or a v0.7 database with populated undated income requires data reconciliation before it can be upgraded. The application refuses these upgrades and preserves the existing rows and version.

The checked-in `db/pvledger.db` is a seeded v0.7 fixture. To try it, copy it to a working location and point `database.sqlite.path` at the copy. Startup backs up the file, upgrades its empty income tables, installs the v0.7.2 posting triggers, and opens setup without reapplying the seeds.

## Extensions and specifications

A manifest-based plugin can contribute a namespaced page and a menu link. Copy the [sample](src/plugins/sample/plugin.yaml) to a directory under `paths.plugins`, give it a distinct ID, and restart. Enable it in Administration → Plugins. The manifest API v1 accepts `id`, `name`, `api_version`, `menu_label`, and `text`; the page renders the supplied text as plain text. Enable and disable take effect immediately.

See the [application specification](docs/spec/pvledger_application_spec.md) and [database specification](docs/spec/pvledger_database_spec.md).
