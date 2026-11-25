"""Run PVLedger over HTTP against disposable databases and migration fixtures."""

import contextlib
import csv
import io
import hashlib
import http.client
import json
import platform
import re
import shutil
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import zipfile
from urllib.parse import urlencode
from xml.etree import ElementTree

BINARY = Path(sys.argv[1]).resolve()
ROOT = Path(__file__).resolve().parents[1]
PASSWORD = "local-integration-password"
SCHEMA = (ROOT / "db/pvledger.sql").read_text()


class Client:
    def __init__(self, port):
        self.port = port
        self.cookie = ""
        self.csrf = ""

    def request(self, path, status=200, method="GET", payload=None, csrf=None, form=False):
        headers = {"X-CSRF-Token": self.csrf if csrf is None else csrf, "Cookie": self.cookie}
        body = None
        if payload is not None:
            headers["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json"
            if isinstance(payload, bytes):
                headers["Content-Type"] = "application/octet-stream"
                body = payload
            else:
                body = urlencode(payload) if form else json.dumps(payload)
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        try:
            connection.request(method, path, body=body, headers=headers)
            reply = connection.getresponse()
            cookie = reply.getheader("Set-Cookie")
            self.mime = reply.getheader("Content-Type", "").split(";")[0]
            text = reply.read()
            if self.mime.startswith("text/") or self.mime in ("application/json", "application/x-ofx",
                                                               "application/qif", "application/vnd.intu.qfx"):
                text = text.decode()
            assert reply.status == status, (method, path, status, reply.status, text)
            self.attachment = reply.getheader("Content-Disposition")
            if cookie is not None:
                self.cookie = cookie.split(";")[0]
            if text and "application/json" in reply.getheader("Content-Type", ""):
                return json.loads(text)
            return text
        finally:
            connection.close()

    def login(self, username):
        result = self.request("/api/v1/auth/login", method="POST",
                              payload={"username": username, "password": PASSWORD}, csrf="login")
        self.csrf = result["csrf"]


def config(directory):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    path = directory / "config.yaml"
    path.write_text(f"""app:
  host: 127.0.0.1
  port: {port}
database:
  engine: sqlite
  sqlite:
    path: ledger.sqlite
paths:
  languages: {ROOT / 'lang'}
  plugins: {ROOT / 'src/plugins'}
  themes: {ROOT / 'themes'}
""")
    return path, port


@contextlib.contextmanager
def server(path, port):
    process = subprocess.Popen([BINARY, "-c", path], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    try:
        for _ in range(100):
            if process.poll() is not None:
                raise AssertionError(process.stdout.read())
            try:
                Client(port).request("/api/v1/system/health")
                break
            except OSError:
                time.sleep(0.03)
        else:
            raise AssertionError("HTTP startup timed out")
        yield Client(port)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise AssertionError("Server did not shut down")


def verify_schema(db):
    tables = [name for name, in db.execute(
        "SELECT name FROM sqlite_schema WHERE type='table' AND name LIKE 'pvl_%'")]
    assert len(tables) == 20
    columns = sum(len(db.execute(f"PRAGMA table_info({table})").fetchall()) for table in tables)
    keys = sum(len(db.execute(f"PRAGMA foreign_key_list({table})").fetchall()) for table in tables)
    assert (columns, keys) == (119, 26)
    assert {r[0] for r in db.execute("SELECT name FROM sqlite_schema WHERE type='trigger'")} == {
        "pvl_post_expense", "pvl_post_income", "pvl_current_balance_history", "pvl_savings_balance_history"}
    assert list(db.execute("PRAGMA foreign_key_check")) == []
    for table, count in [("pvl_countries", 249), ("pvl_currencies", 178), ("pvl_user_roles", 3)]:
        assert db.execute(f"SELECT count(*) FROM {table}").fetchone()[0] == count


def verify_workbook(client, selection, names):
    for fmt in ("ods", "xlsx"):
        payload = {**selection, "format": fmt}
        preview = client.request("/api/v1/export/preview", method="POST", payload=payload)
        assert preview["total"] == 2
        data = client.request("/api/v1/export/download", method="POST", payload=payload)
        assert client.attachment == f'attachment; filename="pvledger-expenses.{fmt}"'
        with zipfile.ZipFile(io.BytesIO(data)) as book:
            assert book.testzip() is None
            for path in book.namelist():
                if path.endswith((".xml", ".rels")):
                    ElementTree.fromstring(book.read(path))
            if fmt == "xlsx":
                assert client.mime == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
                sheet = ElementTree.fromstring(book.read("xl/worksheets/sheet1.xml"))
                rows = sheet.findall(".//{*}row")
                assert [c.findtext(".//{*}t") for c in rows[0]] == selection["columns"]
                for row, name in zip(rows[1:], names):
                    assert row[0].findtext(".//{*}t") == "Checking"
                    assert row[1].get("t") == "inlineStr" and row[1].findtext(".//{*}t") == name
                    assert row[2].findtext("{*}v") == "12.5" and row[2].get("t") is None
                assert sheet.find(".//{*}f") is None
            else:
                assert book.namelist()[0] == "mimetype" and book.getinfo("mimetype").compress_type == zipfile.ZIP_STORED
                assert book.read("mimetype").decode() == client.mime == "application/vnd.oasis.opendocument.spreadsheet"
                # LibreOffice identifies the ODS from the first local ZIP header, before the directory.
                assert int.from_bytes(data[22:26], "little") == len(client.mime)
                assert data[38:38 + len(client.mime)].decode() == client.mime
                sheet = ElementTree.fromstring(book.read("content.xml"))
                rows = sheet.findall(".//{*}table-row")
                assert [c.findtext("{*}p") for c in rows[0]] == selection["columns"]
                office = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
                for row, name in zip(rows[1:], names):
                    assert row[0].findtext("{*}p") == "Checking"
                    assert row[1].get(office + "value-type") == "string"
                    paragraph = row[1].find("{*}p")
                    assert (paragraph.text or "") + "".join(" " + (space.tail or "") for space in paragraph) == name
                    assert row[2].get(office + "value-type") == "float" and row[2].get(office + "value") == "12.5"
            assert len(rows) == 3


def data_workflows(client, account, savings, database):
    api = "/api/v1/"
    assert client.request(api + "lookups/users")[0] == {"id": 1, "label": "admin"}
    assert len(client.request(api + "lookups/countries?page=3")) == 49
    assert client.request(api + "lookups/accounts/current") == [{"id": account, "label": "Checking"}]
    current = api + f"accounts/current/{account}"
    opening = client.request(current)["amount"]
    existing_history = client.request(current + "/history")
    category = client.request(api + "expense-categories", 201, "POST", {"name": "Food"})["id"]
    names = ['=SUM(1,2)', 'Обед & <чай>, "takeaway"']
    valid = {"name": names[0], "amount": 12.5, "date": 1700000000, "source_id": "A"}
    mapping = {"dataset": "expenses", "id_category": category, "id_currency": 1, "id_account": account}
    payload = {**mapping, "rows": [valid, valid, {**valid, "date": "unknown"},
                                   {**valid, "currency": "unmapped"}, {**valid, "name": names[1], "source_id": "B"}]}
    preview = client.request(api + "import/preview", method="POST", payload=payload)
    assert [r.get("duplicate") for r in preview["rows"]] == [False, True, None, None, False]
    assert preview["rows"][2]["error"] and preview["rows"][3]["error"] == "currency_mismatch"
    assert client.request(api + "expenses")["total"] == 0
    commit = {"token": preview["token"], "rows": [0, 4], "confirm": True}
    client.request(api + "import/commit", 422, "POST", {**commit, "rows": [0, 2]})
    client.request(api + "import/commit", 409, "POST", {**commit, "confirm": False})
    client.request(api + "import/commit", 403, "POST", commit, csrf="")
    # A failure to append the second snapshot must also undo the first posting.
    with sqlite3.connect(database) as db:
        db.execute(f"""CREATE TRIGGER fail_second_snapshot BEFORE INSERT ON pvl_account_history_current
                    WHEN NEW.amount={opening - 25} BEGIN SELECT RAISE(ABORT,'test rollback'); END""")
    client.request(api + "import/commit", 409, "POST", commit)
    assert client.request(current)["amount"] == opening
    assert client.request(current + "/history") == existing_history
    assert client.request(api + "expenses")["total"] == 0
    with sqlite3.connect(database) as db:
        db.execute("DROP TRIGGER fail_second_snapshot")
    start = int(time.time())
    assert client.request(api + "import/commit", method="POST", payload=commit) == {"imported": 2, "skipped": 3, "failed": 0}
    client.request(api + "import/commit", 409, "POST", commit)
    history = client.request(current + "/history")
    assert history["total"] == existing_history["total"] + 2
    assert [row["amount"] for row in history["items"][:2]] == [opening - 25, opening - 12.5]
    assert all(start <= row["date"] <= int(time.time()) for row in history["items"][:2])
    preview = client.request(api + "import/preview", method="POST", payload={**mapping, "rows": [valid]})
    assert preview["rows"][0]["duplicate"]
    client.request(api + "import/preview", 422, "POST", {**payload, "dataset": "income"})
    other_expense = {"id_currency": 2, "id_account": account, "id_category": category,
                     "name": "Other", "amount": 7, "date": 1700000001}
    assert client.request(api + "expenses", 422, "POST", other_expense)["error"]["code"] == "posting_currency_mismatch"
    client.request(api + "import/preview", 422, "POST", {**payload, "id_currency": 2})
    other = client.request(api + "accounts/current", 201, "POST", {
        "name": "Other currency", "id_bank": client.request(current)["id_bank"], "id_user": 1, "id_currency": 2, "amount": 0})["id"]
    client.request(api + "expenses", 201, "POST", {**other_expense, "id_account": other})
    assert client.request(api + f"accounts/current/{other}")["amount"] == -7
    groups = client.request(api + "reports/expense-categories")["items"]
    assert [(r["id_currency"], r["category"], r["amount"]) for r in groups] == [(1, "Food", 25), (2, "Food", 7)]
    assert client.request(api + "reports/expense-categories?from=1700000001&id_currency=1")["items"] == []
    for kind, parent, amounts in [("current", account, [220.5, 221]), ("savings", savings, [900])]:
        rows = client.request(api + f"reports/balances?account_type={kind}&id_account={parent}&to=1700000001")["items"]
        assert [r["amount"] for r in rows] == amounts
    expense_export = {"dataset": "expenses", "columns": ["id_account_label", "name", "amount"],
                      "filters": {"id_currency": 1}, "format": "csv"}
    output = client.request(api + "export/preview", method="POST", payload=expense_export)
    assert output["total"] == 2 and all(r["id_account_label"] == "Checking" for r in output["items"])
    output = client.request(api + "export/download", method="POST", payload=expense_export)
    assert list(csv.reader(io.StringIO(output))) == [expense_export["columns"], ["Checking", "'" + names[0], "12.5"],
                                                    ["Checking", names[1], "12.5"]]
    verify_workbook(client, expense_export, names)
    # Legacy SQLite data may contain 64-bit IDs or characters XML cannot represent.
    with sqlite3.connect(database) as db:
        db.execute("INSERT INTO pvl_expenses_categories(id,name) VALUES(?,?)", (987654321012345678, "bad\uffff"))
    for fmt in ("ods", "xlsx"):
        payload = {"dataset": "expense-categories", "format": fmt}
        client.request(api + "export/download", 422, "POST", {**payload, "columns": ["name"]})
        workbook = client.request(api + "export/download", method="POST", payload={**payload, "columns": ["id"]})
        with zipfile.ZipFile(io.BytesIO(workbook)) as book:
            xml = book.read("content.xml" if fmt == "ods" else "xl/worksheets/sheet1.xml")
            assert b">987654321012345678<" in xml
            assert b"<v>987654321012345678" not in xml and b'office:value="987654321012345678"' not in xml
    with sqlite3.connect(database) as db:
        db.execute("DELETE FROM pvl_expenses_categories WHERE id=?", (987654321012345678,))
    for dataset in ("accounts/current", "accounts/savings", "accounts/credit", "history/current", "history/savings",
                    "income", "planned-income", "planned-expenses", "recurring-expenses", "debts", "taxes", "banks",
                    "countries", "currencies", "expense-categories"):
        result = client.request(api + "export/preview", method="POST", payload={"dataset": dataset})
        assert "password" not in result["columns"]
    selection = {"dataset": "expenses", "filters": {"id_account": account, "id_currency": 1}}
    for format in ("qif", "ofx", "qfx"):
        request = {**selection, "format": format}
        preview = client.request(api + "export/preview", method="POST", payload=request)
        assert preview["total"] == 2 and [row["amount"] for row in preview["items"]] == [-12.5, -12.5]
        body = client.request(api + "export/download", method="POST", payload=request)
        assert client.attachment == f'attachment; filename="pvledger-expenses.{format}"'
        if format == "qif":
            assert body.splitlines() == ["!Type:Bank", "D11/14/2023", "T-12.5", "P" + names[0], "^",
                                        "D11/14/2023", "T-12.5", "P" + names[1], "^"]
        else:
            xml = ElementTree.fromstring(body)
            assert xml.findtext(".//CURDEF")
            assert float(xml.findtext(".//LEDGERBAL/BALAMT")) == opening - 25
            rows = xml.findall(".//STMTTRN")
            assert [row.findtext("NAME") for row in rows] == names
            assert all(row.findtext("DTPOSTED") == "20231114221320" for row in rows)
            assert all(row.findtext("TRNAMT") == "-12.5" and row.findtext("TRNTYPE") == "DEBIT" for row in rows)
            assert len({row.findtext("FITID") for row in rows}) == 2
    body = client.request(api + "export/download", method="POST", payload={"dataset": "income", "format": "ofx"})
    assert ElementTree.fromstring(body).findtext(".//STMTTRN/TRNAMT") == "123.25"
    client.request(api + "export/preview", 422, "POST", {"dataset": "expenses", "format": "ofx"})
    client.request(api + "export/download", 422, "POST", {"dataset": "accounts/current", "format": "qif"})
    client.request(api + "export/download", 422, "POST", {**expense_export, "format": "ofx"})
    client.request(api + "export/download", 403, "POST", {"dataset": "users"})
    client.request(api + "export/download", 422, "POST", {**expense_export, "columns": ["password"]})
    assert client.request(current)["amount"] == opening - 25
    assert client.request(current + "/history") == history

    expense = {"id_account": account, "id_currency": 1, "id_category": category, "name": "Daily expense"}
    client.request(api + "expenses", 422, "POST", {**expense, "amount": None})
    for date, amount in [(-1, 8), (0, 2.5), (3600, -2.5), (None, 10)]:
        client.request(api + "expenses", 201, "POST", {**expense, "date": date, "amount": amount})
    client.request(api + "planned-expenses", 201, "POST",
                   {"id_account": account, "id_currency": 1, "name": "Planned", "amount": 90, "date": 0, "status": 0})
    spending = api + "reports/spending"
    rows = client.request(spending)["items"]
    assert [(r["currency"], r["day"], r["amount"]) for r in rows] == [
        ("AED", -86400, 8), ("AED", 0, 0), ("AED", 1699920000, 25), ("AFN", 1699920000, 7)]
    assert client.request(spending + f"?id_currency=1&id_account={account}&from=3600&to=3600")["items"] == [
        {"id_currency": 1, "currency": "AED", "day": 0, "amount": -2.5}]
    assert client.request(spending + "?id_currency=2")["items"] == rows[-1:]
    assert client.request(spending + "?id_account=99999")["items"] == []
    client.request(spending + "?from=1&to=0", 422)
    today = int(time.time()) // 86400 * 86400
    tomorrow = today + 86400
    base = {"id_account": account, "name": "Estimate boundary"}
    for date in (today - 1, tomorrow - 1, tomorrow):
        client.request(api + "income", 201, "POST", {**base, "amount": 12, "date": date, "status": 1})
        client.request(api + "expenses", 201, "POST", {**expense, "amount": 3, "date": date})
        client.request(api + "planned-income", 201, "POST", {**base, "amount": 40, "date": date, "status": 0})
        client.request(api + "planned-expenses", 201, "POST", {**base, "id_currency": 1, "amount": 10, "date": date})
    for status in (1, 2, 3):
        client.request(api + "planned-income", 201, "POST", {**base, "amount": 90, "date": tomorrow, "status": status})
        client.request(api + "planned-expenses", 201, "POST", {**base, "id_currency": 1, "amount": 60,
                                                              "date": tomorrow, "status": status})
    client.request(api + "planned-income", 201, "POST", {**base, "id_account": other, "amount": 70,
                                                          "date": tomorrow, "status": 0})
    estimates = api + f"reports/estimates?from={today - 1}&to={tomorrow}"
    rows = client.request(estimates)["items"]
    assert [(r["id_currency"], r["day"], r["basis"], r["income"], r["expenses"], r["net"]) for r in rows] == [
        (1, today - 86400, "actual", 12, 3, 9), (1, today, "actual", 12, 3, 9),
        (1, tomorrow, "planned", 40, 10, 30), (2, tomorrow, "planned", 70, 0, 70)]
    assert client.request(estimates + f"&id_account={account}")["items"] == rows[:3]
    assert client.request(estimates + "&id_currency=2")["items"] == rows[3:]
    assert 'data-route="reports/estimates"' in client.request("/reports/estimates")


def backup_workflows(client, directory):
    settings = "/api/v1/settings"
    restore = settings + "/restore"
    setup_restore = "/api/v1/setup/restore"
    assert restore in client.request("/settings")
    data = client.request(settings + "/backup")
    assert client.mime == "application/vnd.sqlite3" and data.startswith(b"SQLite format 3\0")
    assert client.attachment == 'attachment; filename="pvledger-backup.sqlite"'
    saved = directory / "saved.sqlite"
    saved.write_bytes(data)
    with sqlite3.connect(saved) as db:
        verify_schema(db)
        dump = list(db.iterdump())
        assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        db.execute("UPDATE pvl_users SET password='pbkdf2-sha256$invalid' WHERE id_role=1")
    second = Client(client.port)
    second.login("viewer")
    second.request(settings + "/backup", 403)
    second.request(restore, 403, "POST", data)
    client.request(setup_restore, 404, "POST", data)
    client.request(restore, 403, "POST", data, csrf="wrong")
    client.request(restore, 415, "POST", {})
    client.request(settings, method="PATCH", payload={"instance_name": "After backup"})
    for invalid in (b"", b"not a database", data[:100], data[:-1], saved.read_bytes()):
        client.request(restore, 422, "POST", invalid)
        assert client.request(settings)["instance_name"] == "After backup"
    client.request(restore, 204, "POST", data)
    client.request(settings, 401)
    second.request("/api/v1/auth/me", 401)
    client.login("admin")
    assert client.request(settings)["instance_name"] == "<Local & private>"
    with sqlite3.connect(directory / "ledger.sqlite") as db:
        assert list(db.iterdump()) == dump

    with tempfile.TemporaryDirectory() as new:
        destination = Path(new)
        path, port = config(destination)
        with server(path, port) as fresh:
            form = fresh.request("/setup")
            token = re.search(r'name="csrf" value="([a-f0-9]{64})"', form)[1]
            assert setup_restore in form
            fresh.request(setup_restore, 403, "POST", data, csrf="wrong")
            fresh.request(setup_restore, 422, "POST", b"bad backup", csrf=token)
            assert fresh.request("/api/v1/system/health")["status"] == "setup"
            fresh.request(setup_restore, 204, "POST", data, csrf=token)
            fresh.request("/setup", 404)
            fresh.request(setup_restore, 404, "POST", data, csrf=token)
        with server(path, port) as restored:
            restored.login("admin")
            assert restored.request(settings)["instance_name"] == "<Local & private>"
            with sqlite3.connect(destination / "ledger.sqlite") as db:
                assert list(db.iterdump()) == dump


def exercise(directory):
    path, port = config(directory)
    database = directory / "ledger.sqlite"
    with server(path, port) as client:
        client.request("/", 303)
        client.request("/api/v1/income", 404)
        for locale, title in [("ru_RU", "Первоначальная настройка"), ("kk_KZ", "Бастапқы баптау"),
                              ("de_DE", "Ersteinrichtung")]:
            assert title in client.request("/setup?lang=" + locale)
        page = client.request("/setup")
        assert "Manage your finances privately" in page
        setup_token = re.search(r'name="csrf" value="([a-f0-9]{64})"', page)[1]
        fields = {"lang": "en_US", "theme": "default", "default_currency": "1",
                  "username": "admin", "password": PASSWORD, "instance_name": "<Local & private>"}
        client.request("/setup", 422, "POST", fields, form=True)
        fields["csrf"] = setup_token
        client.request("/setup", 303, "POST", fields, form=True)
        client.request("/api/v1/income", 401)
        client.request("/api/v1/reports/spending", 401)
        client.request("/setup", 404)
        client.request("/api/v1/auth/login", 403, "POST", {"username": "admin", "password": PASSWORD})
        client.login("admin")
        assert "&lt;Local &amp; private&gt;" in client.request("/")
        about = client.request("/about")
        version = client.request("/api/v1/system/version")["version"]
        assert subprocess.check_output([BINARY, "--version"], text=True).strip() == "PVLedger " + version
        assert f"<p>{version}</p>" in about and "Manage your finances privately" in about
        assert "(c) 2026 Vadim Prishlyak" in about
        new_tab = 'target="_blank" rel="noopener noreferrer"'
        assert f'<a href="mailto:pvadimr@pvadimr.com" {new_tab}>pvadimr@pvadimr.com</a>' in about
        source_url = "https://forge.pvadimr.com/pvledger/pvledger"
        assert f'<a href="{source_url}" {new_tab}>{source_url}</a>' in about
        assert (f'<a href="https://www.gnu.org/licenses/agpl-3.0.html" {new_tab}>'
                'GNU Affero General Public License v3.0 (AGPL-3.0-only)</a>') in about
        assert "<h2>System information</h2>" in about
        if sys.platform == "win32":
            assert re.search(r"<dd>Windows(?: build [0-9]+)?</dd>", about)
            assert "<dd>x86_64</dd>" in about or "<dd>arm64</dd>" in about
        else:
            assert f"<dd>{platform.system()} {platform.release()}</dd>" in about
            assert f"<dd>{platform.machine()}</dd>" in about
        assert re.search(r"<dt>SQLite version</dt><dd>\d+\.\d+\.\d+", about)
        assert "<dt>Database schema version</dt><dd>0.7.2</dd>" in about
        assert client.request("/assets/js/app.js").startswith('"use strict"')
        api = "/api/v1/"
        menu = client.request(api + "ui/menu")
        assert len(menu) > 10 and {"href": "/reports/spending", "label": "Spending over time", "group": "Reports"} in menu
        assert 'data-route="reports/spending"' in client.request("/reports/spending")
        assert client.request(api + "i18n/de_DE")["dashboard"] == "Übersicht"
        assert client.request(api + "dashboard")["current_accounts"] == []
        assert "password" not in client.request(api + "users/1")
        client.request(api + "banks", 403, "POST", {"name": "Blocked", "id_countries": 1}, csrf="")
        client.request(api + "banks", 409, "POST", {"name": "Bad reference", "id_countries": 99999})
        bank = client.request(api + "banks", 201, "POST", {"name": "Bank", "id_countries": 1})["id"]
        account = client.request(api + "accounts/current", 201, "POST",
                                 {"name": "Checking", "id_bank": bank, "id_currency": 1,
                                  "id_user": 1, "amount": 500.5})["id"]
        savings = client.request(api + "accounts/savings", 201, "POST",
                                 {"name": "Savings", "id_bank": bank, "id_currency": 2,
                                  "id_user": 1, "amount": 200})["id"]
        client.request(api + f"banks/{bank}", 409, "DELETE")
        client.request(api + "planned-expenses", 422, "POST",
                       {"name": "Expense", "id_account": account, "id_currency": 1, "status": 4})
        for role, username in [(2, "writer"), (3, "viewer")]:
            client.request(api + "users", 201, "POST",
                           {"username": username, "password": PASSWORD, "id_role": role})
        client.request(api + "settings", 200, "PATCH", {"lang": "de_DE"})
        assert "Übersicht" in client.request("/")
        client.request(api + "settings", 200, "PATCH", {"lang": "en_US"})
        assert '<option value="dark">Dark</option>' in client.request("/settings")
        client.request(api + "settings", 200, "PATCH", {"theme": "dark", "default_currency": 2})
        assert client.request(api + "auth/me")["default_currency"] == 2
        dark_css = client.request("/assets/theme.css")
        assert "color-scheme: dark" in dark_css and ".form-panel" in dark_css
        plugins = client.request(api + "plugins")
        sample = next(plugin for plugin in plugins if plugin["path"] == "example.sample")
        client.request("/plugins/example.sample", 404)
        client.request(api + f"plugins/{sample['id']}/enable", method="POST")
        assert "local PVLedger plugin" in client.request("/plugins/example.sample")
        client.request(api + f"plugins/{sample['id']}/disable", method="POST")
        client.request("/plugins/example.sample", 404)
        with sqlite3.connect(database) as db:
            db.execute("PRAGMA foreign_keys=ON")
            verify_schema(db)
            assert db.execute("SELECT count(*) FROM pvl_settings").fetchone() == (1,)
            for name in ("pvl_income", "pvl_income_planned"):
                db.execute(f"INSERT INTO {name}(id_account,name,amount,status,date) VALUES(?,?,?,?,?)",
                           (account, "Income", 123.25, 17, 1_700_000_000))
                for key in ("id_account", "name", "amount", "status", "date"):
                    try:
                        db.execute(f"UPDATE {name} SET {key}=NULL")
                    except sqlite3.IntegrityError:
                        continue
                    raise AssertionError(f"{name}.{key} allowed NULL")
            db.executemany("INSERT INTO pvl_account_history_current(id_account,date,amount) VALUES (?,?,?)",
                           [(account, 1_700_000_000, 220.5), (account, 1_700_000_000, 221)])
            db.execute("INSERT INTO pvl_account_history_savings(id_account,date,amount) VALUES (?,?,?)",
                       (savings, 1_700_000_001, 900))
        for route in ("income", "planned-income"):
            result = client.request(api + route + f"?id_account={account}&from=1700000000&status=17")
            assert result["total"] == 1 and result["items"][0]["status"] == 17
            assert result["writable"]
            assert result["items"][0]["date"] == 1_700_000_000
            legacy = result["items"][0]
            assert client.request(api + route + f"/{legacy['id']}", 200, "PATCH",
                                  {"description": "Legacy record"})["status"] == 17
            entry = {"id_account": account, "name": "Salary", "amount": 42.75, "date": 1735689600, "status": 0}
            for key in entry:
                client.request(api + route, 422, "POST", {k: v for k, v in entry.items() if k != key})
            client.request(api + route, 422, "POST", {**entry, "status": 4})
            created = client.request(api + route, 201, "POST", entry)
            assert all(created[key] == value for key, value in entry.items())
            detail = api + route + f"/{created['id']}"
            assert client.request(detail, 200, "PATCH", {"status": 2})["status"] == 2
            client.request(detail, 204, "DELETE")
        # Both a direct SQL actual income insert and the API insert posted once.
        assert client.request(api + f"accounts/current/{account}")["amount"] == 666.5
        assert [r["amount"] for r in client.request(api + f"accounts/current/{account}/history")["items"]] == [
            666.5, 623.75, 221, 220.5]
        assert client.request(api + f"accounts/savings/{savings}/history")["items"][0]["amount"] == 900
        data_workflows(client, account, savings, database)
        for kind, parent in [("current", account), ("savings", savings)]:
            target = api + f"accounts/{kind}/{parent}"
            earlier = client.request(target + "/history")["items"]
            start = int(time.time())
            client.request(target, 200, "PATCH", {"amount": 91.25})
            client.request(target, 200, "PATCH", {"amount": 91.25, "description": "Reviewed"})
            client.request(target, 422, "PATCH", {"amount": None})
            if kind == "current":
                client.request(api + "income", 201, "POST",
                               {"id_account": parent, "name": "Zero", "date": 0, "amount": 0, "status": 1})
            rows = client.request(target + "/history")["items"]
            assert rows[1:] == earlier
            assert rows[0]["id_account"] == parent and rows[0]["amount"] == client.request(target)["amount"] == 91.25
            assert start <= rows[0]["date"] <= int(time.time())
        client.request(api + "auth/logout", method="POST")
        client.request(api + "auth/me", 401)
        client.login("viewer")
        assert client.request(api + "lookups/accounts/current") == []
        assert client.request(api + "lookups/users") == [{"id": 3, "label": "viewer"}]
        assert client.request(api + "reports/balances")["items"] == []
        assert client.request(api + "reports/expense-categories")["items"] == []
        assert client.request(api + "reports/estimates")["items"] == []
        assert client.request(api + f"reports/estimates?id_account={account}")["items"] == []
        for fmt in ("ods", "xlsx"):
            assert client.request(api + "export/preview", method="POST",
                                  payload={"dataset": "expenses", "format": fmt})["total"] == 0
        assert client.request(api + f"reports/spending?id_account={account}")["items"] == []
        assert client.request(api + "export/preview", method="POST", payload={"dataset": "income"})["total"] == 0
        client.request(api + "import/preview", 403, "POST", {})
        client.request("/data/import", 403)
        assert client.request(api + "income")["total"] == 0
        client.request(api + "income", 403, "POST", {"id_account": account})
        client.request(api + f"accounts/current/{account}/history", 404)
        client.request(api + "banks", 403, "POST", {"name": "Viewer", "id_countries": 1})
        client.login("writer")
        client.request(api + "import/preview", 422, "POST", {"dataset": "expenses", "id_account": account,
                       "id_currency": 1, "id_category": 1, "rows": []})
        client.request(api + "settings", 403, "PATCH", {"lang": "ru_RU"})
        client.request(api + "income/1", 404)
        client.request(api + "debts", 422, "POST", {"name": "Other account", "id_account": account})
        owned_account = client.request(api + "accounts/current", 201, "POST",
                                       {"name": "Owned by writer", "id_bank": bank, "id_currency": 1})["id"]
        owned = api + f"accounts/current/{owned_account}"
        entry = {"id_account": owned_account, "name": "Owned income", "amount": 30, "date": 0, "status": 1}
        assert client.request(api + "income", 422, "POST", entry)["error"]["code"] == "opening_balance_required"
        client.request(owned, 200, "PATCH", {"amount": 0})
        for route in ("income", "planned-income"):
            client.request(api + route, 422, "POST", {**entry, "id_account": account})
            client.request(api + route, 201, "POST", entry)
        assert [r["amount"] for r in client.request(owned + "/history")["items"]] == [30, 0]
        owned_rows = client.request(api + "reports/estimates")["items"]
        assert owned_rows == [{"id_currency": 1, "currency": "AED", "day": 0,
                               "basis": "actual", "income": 30, "expenses": 0, "net": 30}]

    with server(path, port) as client:
        client.login("admin")
        assert client.request("/api/v1/settings")["instance_name"] == "<Local & private>"
        assert client.request("/api/v1/settings")["theme"] == "dark"
        assert client.request("/api/v1/accounts/current")["total"] == 3
        assert client.request(owned)["amount"] == 30
        assert client.request(owned + "/history")["total"] == 2
        both = client.request(api + "reports/estimates?from=0&to=3600&id_currency=1")["items"]
        assert len(both) == 1 and both[0]["income"] == 30 and both[0]["expenses"] == 0
        with sqlite3.connect(database) as db:
            verify_schema(db)
        backup_workflows(client, directory)


def migration(directory, version, blocked=False):
    path, port = config(directory)
    database = directory / "ledger.sqlite"
    with sqlite3.connect(database) as db:
        db.executescript(SCHEMA)
        # These fixtures represent versions before the posting rules were installed.
        for name, in db.execute("SELECT name FROM sqlite_schema WHERE type='trigger'").fetchall():
            db.execute(f"DROP TRIGGER {name}")
        db.execute("PRAGMA foreign_keys=OFF")
        db.execute("INSERT INTO pvl_users(id,username,password,id_role) VALUES(1,'admin',?,1)",
                   (hashlib.sha256(PASSWORD.encode()).hexdigest(),))
        db.execute("INSERT INTO pvl_settings(id,db_version,instance_name) VALUES(1,?,'Legacy ledger')", (version,))
        db.execute("INSERT INTO pvl_banks(id,name,id_countries) VALUES (1,'Legacy bank',1)")
        db.execute("INSERT INTO pvl_accounts_current(id,id_bank,id_user,id_currency,name,amount) "
                   "VALUES(1,1,1,1,'Legacy current',33)")
        db.execute("INSERT INTO pvl_account_history_current(id,id_account,date,amount) VALUES (7,1,100,10.5)")
        if version == "0.7.1":
            db.execute("INSERT INTO pvl_income(id_account,name,amount,status,date) VALUES(1,'Existing income',12,1,100)")
        if version in {"0.5", "0.6", "0.6.1"}:
            db.execute("DROP TABLE pvl_income")
            db.execute("DROP TABLE pvl_income_planned")
        if version == "0.5":
            db.execute("DROP TABLE pvl_account_history_current")
            db.execute("DROP TABLE pvl_account_history_savings")
        if version == "0.6":
            db.execute("DROP TABLE pvl_account_history_current")
            db.execute("CREATE TABLE pvl_account_history_current (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "id_account INTEGER REFERENCES pvl_accounts_current(id), date INTEGER NOT NULL, amount REAL NOT NULL)")
            db.execute("INSERT INTO pvl_account_history_current(id,id_account,date,amount) VALUES (7,?,100,10.5)",
                       (None if blocked else 1,))
        if version == "0.7":
            for table in ("pvl_income", "pvl_income_planned"):
                db.execute(f"ALTER TABLE {table} DROP COLUMN date")
            if blocked:
                db.execute("INSERT INTO pvl_income_planned(id_account,name,amount,status) VALUES(1,'Legacy',14,9)")
    if blocked:
        failure = subprocess.run([BINARY, "-c", path], capture_output=True, text=True, timeout=5)
        assert failure.returncode != 0 and "Migration blocked:" in failure.stderr, failure.stderr
        with sqlite3.connect(database) as db:
            assert db.execute("SELECT db_version FROM pvl_settings").fetchone() == (version,)
            if version == "0.7":
                assert db.execute("SELECT name,amount FROM pvl_income_planned").fetchone() == ("Legacy", 14)
                assert not any(row[1] == "date" for row in db.execute("PRAGMA table_info(pvl_income)"))
            else:
                assert db.execute("SELECT id_account,amount FROM pvl_account_history_current").fetchone() == (None, 10.5)
    else:
        with server(path, port) as client:
            client.login("admin")
            assert client.request("/api/v1/settings")["db_version"] == "0.7.2"
            assert client.request("/api/v1/accounts/current/1")["amount"] == 33
            history = client.request("/api/v1/accounts/current/1/history")
            if version != "0.5":
                assert history["total"] == 1
                assert history["items"][0]["id"] == 7 and history["items"][0]["amount"] == 10.5
            if version == "0.7.1":
                assert client.request("/api/v1/income")["total"] == 1
                client.request("/api/v1/income", 201, "POST", {"id_account": 1, "name": "New", "amount": 12, "status": 1, "date": 100})
                assert client.request("/api/v1/accounts/current/1/history")["items"][0]["amount"] == 45
                backup, = directory.glob("*.backup")
                client.request("/api/v1/settings/restore", 204, "POST", backup.read_bytes())
                client.login("admin")
                assert client.request("/api/v1/settings")["db_version"] == "0.7.2"
                assert client.request("/api/v1/income")["total"] == 1
                assert client.request("/api/v1/accounts/current/1")["amount"] == 33
            with sqlite3.connect(database) as db:
                verify_schema(db)
                assert db.execute("SELECT password FROM pvl_users").fetchone()[0].startswith("pbkdf2-sha256$")
    backup, = list(directory.glob("*.backup"))
    with sqlite3.connect(backup) as db:
        assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        assert db.execute("SELECT db_version FROM pvl_settings").fetchone() == (version,)


with tempfile.TemporaryDirectory() as temporary:
    exercise(Path(temporary))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    database = directory / "ledger.sqlite"
    shutil.copyfile(ROOT / "db/pvledger.db", database)
    path, port = config(directory)
    with server(path, port) as client:
        assert client.request("/api/v1/system/health")["status"] == "setup"
        with sqlite3.connect(database) as db:
            verify_schema(db)
            for table in ("pvl_income", "pvl_income_planned"):
                date = next(field for field in db.execute(f"PRAGMA table_info({table})") if field[1] == "date")
                assert date[2:4] == ("INTEGER", 1)
            assert db.execute("SELECT count(*) FROM pvl_settings").fetchone() == (0,)
for version, blocked in [("0.5", False), ("0.6", False), ("0.6", True),
                         ("0.6.1", False), ("0.7", False), ("0.7", True), ("0.7.1", False)]:
    with tempfile.TemporaryDirectory() as temporary:
        migration(Path(temporary), version, blocked)
