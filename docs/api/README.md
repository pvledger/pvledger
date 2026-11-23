# PVLedger JSON API

Send JSON objects to `/api/v1`. Log in with `POST /auth/login`, body `{"username":"…","password":"…"}`, and header `X-CSRF-Token: login`. Retain the session cookie and the returned `csrf` value. Send that token as `X-CSRF-Token` with authenticated writes. You can also retrieve it with `GET /auth/me`.

Collection responses expose `items`, `page`, `page_size`, `total`, and resource field metadata. Set `page_size` between 1 and 100. Sort using `sort=date` or `sort=-date` for resources with a date field. Income can be filtered using `id_account`, integer `status`, and inclusive `from` and `to` UTC epoch seconds. Account-history lists accept the date filters, use a fixed page size of 50, and return the parent account's currency.

Standard collection writes use POST, detail updates use PATCH, and detail deletion uses DELETE. Use the column names from the [database specification](../specs/pvledger_database_spec.md) as keys and omit `id` from request bodies. PostgreSQL and MariaDB/MySQL adapters are deferred. The income and account-history endpoints are read-only while status and capture rules remain open.

Requests can return an error object of the form `{"error":{"code":"constraint","message":"…"}}`. Use the code for client-side decisions since messages follow the instance locale. A successful deletion returns status 204.

See the [OpenAPI document](openapi.yaml) for routes and response codes.
