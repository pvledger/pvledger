# PVLedger

PVLedger is a self-hosted ledger for personal accounts, balance history, income, expenses, debts, and taxes. The C++17 application serves its browser interface and JSON API with Oat++ and stores data locally in SQLite.

Copyright (c) 2026 Vadim Prishlyak. PVLedger is licensed under the GNU Affero General Public License version 3 (AGPL-3.0-only). See [LICENSE](LICENSE) for the full text.

## Build and run

The supported release target is Linux x86-64. Install a C++17 compiler with static C/C++ runtimes, binutils, CMake 3.18+, Git, pkg-config, and the SQLite, OpenSSL and JsonCpp development packages. The release needs the SQLite and libcrypto static archives. CMake uses an installed JsonCpp archive or builds JsonCpp 1.9.5. Python 3.8+ runs the HTTP integration suite. CTest also exercises the browser file parsers when it finds a Chromium-based browser. CMake uses installed Oat++ 1.3.0 and yaml-cpp 0.8 if available, or fetches the pinned release tags.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pvledger -c src/config/config.yaml
```

Open `http://127.0.0.1:8080` to choose a language, set preferences, and create the first administrator. The example configuration creates a fresh database in `src/config/data`. To bootstrap a missing configuration, pass a new YAML filename in an existing writable directory. PVLedger uses port 8080, its distributed assets, and `data/pvledger.sqlite` relative to that directory, then writes the configuration when setup is submitted. To select a different database or port, provide a configuration file before starting. Setup listens on loopback and closes after the first administrator is created. Restart after setup to apply any non-loopback binding in an existing configuration.

The default static build verifies the final ELF has no interpreter or shared-library dependencies. Supply a static library prefix through `CMAKE_PREFIX_PATH` if needed. Use `-DPVLEDGER_STATIC=OFF` for a dynamically linked development build.

Use `cmake --install build` to install the executable and assets. Set `CMAKE_INSTALL_PREFIX` at configure time for a custom installation. Immutable resources, including templates, scripts, and the initial SQL, are installed under the configured data directory.

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

The JSON API uses `/api/v1`. Income and balance history are available to browse and filter. Income status is shown as its stored integer. Expenses, income, live balances, and historical balances are managed as distinct records. Dates use UTC epoch seconds and the dashboard groups totals by currency. See the [API contract](docs/api/README.md) and [OpenAPI document](docs/api/openapi.yaml).

## Import, export and reports

Open **Data → Import** to load a local CSV, QIF, OFX or QFX file. Choose the file encoding, then map the destination current account, expense category, currency, date format and expense sign. For CSV, select the delimiter, header and source columns. The preview shows each mapped expense, row errors and possible duplicates. Select the rows to save and confirm once. Download the row details to retain the review. Imports support up to 500 records in a 512 KiB file.

The first import destination is actual expenses. The wizard accepts bank, cash and credit-card transaction QIF sections, and a single bank or credit-card statement in OFX 1.x SGML or OFX 2.x XML, including QFX files with those structures. A positive expense is stored in the explicitly selected currency. Source currency conflicts, unsupported splits, investment records, corrections and unrepresentable dates require review. For QIF-style two-digit years, 70–99 select 1970–1999 and 00–69 select 2000–2069. OFX timezone offsets are applied before storage. Preview flags matching account, currency, name, timestamp and amount, as well as repeated source identifiers within the file. The existing schema has no persistent external transaction ID. Importing expenses leaves live balances and snapshots independent.

**Data → Export** offers UTF-8 CSV for accounts, histories, financial entries and reference data. Choose the filters and columns, review the count and sample, then download. Relationship columns include IDs and selectable readable labels. Dates use UTC epoch seconds in the CSV. A download holds up to 5,000 rows and 8 MiB. Larger datasets can be split with filters.

**Reports** shows actual expenses by category within each currency and recorded balances for each current or savings account. Each chart has a matching table. Category totals containing negative values or lacking a positive sum are shown in the table alone. Balance charts retain every snapshot, ordering equal timestamps by snapshot ID. Lines connect observations and the table lists their recorded times. Filters apply to the signed-in user's authorized records.

## Optional Docker deployment

Docker Compose publishes container port 8080 on the host at `127.0.0.1:8080`. The `PVLEDGER_CONTAINER_BIND` setting lets the setup wizard and the default service listen on the container network while retaining their local Host header checks. Check that host port 8080 is available.

```sh
docker compose up --build -d
docker compose logs -f pvledger
docker compose down
```

Open `http://127.0.0.1:8080/setup`. A named volume stores configuration and SQLite files under `/var/lib/pvledger`. The image runs as the unprivileged `pvledger` user. If you use a bind mount, give that user access to its directory and the files it contains. When changing the volume's configuration, keep the host bind address at `127.0.0.1` for local access. For remote access, place the app behind an HTTPS reverse proxy and enable `app.secure_cookie`.

## Backups and upgrades

Stop PVLedger before copying its database and configuration. Alternatively, use SQLite's online backup command while the application is running.

```sh
sqlite3 /path/to/pvledger.sqlite '.backup /path/to/pvledger.backup'
sqlite3 /path/to/pvledger.backup 'PRAGMA integrity_check; PRAGMA foreign_key_check;'
```

To restore, stop the application and point its configuration at a verified copy of the backup. Keep the old file until the restored instance starts and its records have been checked. Automatic upgrades create a sibling SQLite backup before changing the schema. A v0.6 database with unlinked history snapshots or a v0.7 database with populated undated income requires data reconciliation before it can be upgraded. The application refuses these upgrades and preserves the existing rows and version.

The checked-in `db/pvledger.db` is a seeded v0.7 fixture. To try it, copy it to a working location and point `database.sqlite.path` at the copy. Startup backs up the file, upgrades its empty income tables, and opens setup without reapplying the seeds.

## Extensions and specifications

A manifest-based plugin can contribute a namespaced page and a menu link. Copy the [sample](src/plugins/sample/plugin.yaml) to a directory under `paths.plugins`, give it a distinct ID, and restart. Enable it in Administration → Plugins. The manifest API v1 accepts `id`, `name`, `api_version`, `menu_label`, and `text`; the page renders the supplied text as plain text. Enable and disable take effect immediately.

See the [application specification](docs/specs/pvledger_application_spec.md) and [database specification](docs/specs/pvledger_database_spec.md).
