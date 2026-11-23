# PVLedger JSON API

Send JSON objects to `/api/v1`. Log in with `POST /auth/login`, body `{"username":"…","password":"…"}`, and header `X-CSRF-Token: login`. Retain the session cookie and the returned `csrf` value. Send that token as `X-CSRF-Token` with authenticated writes. You can also retrieve it with `GET /auth/me`.

Collection responses expose `items`, `page`, `page_size`, `total`, and resource field metadata. Set `page_size` between 1 and 100. Sort using `sort=date` or `sort=-date` for resources with a date field. Income can be filtered using `id_account`, integer `status`, and inclusive `from` and `to` UTC epoch seconds. Account-history lists accept the date filters, use a fixed page size of 50, and return the parent account's currency.

Standard collection writes use POST, detail updates use PATCH, and detail deletion uses DELETE. Use the column names from the [database specification](../specs/pvledger_database_spec.md) as keys and omit `id` from request bodies. PostgreSQL and MariaDB/MySQL adapters are deferred. The income and account-history endpoints are read-only while status and capture rules remain open.

Requests can return an error object of the form `{"error":{"code":"constraint","message":"…"}}`. Use the code for client-side decisions since messages follow the instance locale. A successful deletion returns status 204.

## Data workflows

- `GET /lookups/{resource}?page=1` returns up to 100 authorized `{id,label}` entries. Use the collection's foreign-key `resource` metadata to choose a lookup, including `accounts/current` and `accounts/savings`. Non-administrators can resolve their own user label.
- `POST /import/preview` accepts the browser's parsed rows with `dataset: "expenses"`, `id_account`, `id_currency` and `id_category`. Each row contains a name, positive amount and UTC timestamp. Optional fields include description, source currency ISO code, source identifier and a file parser error. The response includes the mapped rows and a ten-minute token. A new preview replaces the previous one in that session.
- `POST /import/commit` accepts `{"token":"…","confirm":true,"rows":[0,2]}`. Indices select rows in preview order, including any duplicates the operator explicitly chose. Validation errors make a row ineligible. A successful atomic commit consumes the token. Account access, reference validity and new duplicate conflicts are checked at commit time.
- `POST /export/preview` and `POST /export/download` accept the same selection, for example `{"dataset":"history/savings","format":"csv","filters":{"id_account":2},"columns":["date","amount","id_account_label","id_currency_label"]}`. Preview returns available columns, the full count, the row limit and up to ten sample rows. Download rechecks visibility and returns a named CSV attachment. The server requires the session CSRF header for both requests.
- `GET /reports/expense-categories` returns category sums per currency. `GET /reports/balances?account_type=savings` returns individual stored snapshot points. Both accept account and currency IDs and inclusive UTC `from` and `to` filters and support up to 5,000 result rows.

See the [OpenAPI document](openapi.yaml) for routes and response codes.
