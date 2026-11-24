# PVLedger changelog

## Unreleased

- Added **Reports → Cash flow estimates** for all accessible current accounts or one selected account. Daily totals show income, expenses and net cash flow by currency. The report uses actual entries through today (UTC) and outstanding plans starting tomorrow.
- Added OpenDocument (.ods) and Excel (.xlsx) downloads to the Export wizard with the same datasets, filters, column selection and preview as CSV. Workbooks preserve literal text, numeric amounts and large identifiers.
- Added a complete SQLite backup download and restore form under **Administration → Settings**. An administrator can replace the ledger from a validated backup of up to 64 MiB. After recovery, users sign in with credentials from the backup.
- Added **Restore from a backup** to initial setup, alongside the option to create an instance and its first administrator.
- Added a commercial licensing option for proprietary modifications alongside AGPL-3.0-only. Commercial agreements are available from the copyright holder. [Licensing terms](LICENSE)
- Actual expenses now debit their current account and actual income credits it. Changes to current and savings balances record a timestamped balance snapshot. Existing installations upgrade to database schema 0.7.2.

## 0.1.1 — 2026-11-24

- Added **Spending over time** with daily actual expense totals grouped by currency.
- Updated the About page with the application version, server platform, SQLite and schema versions, and links to the maintainer, source and license.
