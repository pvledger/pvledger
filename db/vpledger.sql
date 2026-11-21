--
-- File generated with Letos v4.0.4 on Sat Nov 21 12:05:49 2026
--
-- Text encoding used: UTF-8
--
PRAGMA foreign_keys = off;
BEGIN TRANSACTION;

-- Table: pvl_access_rights
DROP TABLE IF EXISTS pvl_access_rights;
CREATE TABLE IF NOT EXISTS "pvl_access_rights" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT);

-- Table: pvl_accounts_credit
DROP TABLE IF EXISTS pvl_accounts_credit;
CREATE TABLE IF NOT EXISTS "pvl_accounts_credit" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES pvl_banks (id), id_user INTEGER REFERENCES pvl_users (id), name TEXT, description TEXT, interest REAL, billing_date INTEGER, amount REAL, id_current_account INTEGER REFERENCES pvl_accounts_current (id), id_currency INTEGER REFERENCES pvl_currencies (id));

-- Table: pvl_accounts_current
DROP TABLE IF EXISTS pvl_accounts_current;
CREATE TABLE IF NOT EXISTS "pvl_accounts_current" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES pvl_banks (id), name TEXT, amount REAL, description TEXT, id_user INTEGER REFERENCES pvl_users (id), id_currency INTEGER REFERENCES pvl_currencies (id));

-- Table: pvl_accounts_savings
DROP TABLE IF EXISTS pvl_accounts_savings;
CREATE TABLE IF NOT EXISTS "pvl_accounts_savings" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES pvl_banks (id), interest REAL, billing_date NUMERIC, name TEXT, description TEXT, id_user INTEGER REFERENCES pvl_users (id), amount REAL, id_currency INTEGER REFERENCES pvl_currencies (id));

-- Table: pvl_banks
DROP TABLE IF EXISTS pvl_banks;
CREATE TABLE IF NOT EXISTS "pvl_banks" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT, id_countries INTEGER REFERENCES pvl_countries (id));

-- Table: pvl_countries
DROP TABLE IF EXISTS pvl_countries;
CREATE TABLE IF NOT EXISTS "pvl_countries" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, iso TEXT);

-- Table: pvl_currencies
DROP TABLE IF EXISTS pvl_currencies;
CREATE TABLE IF NOT EXISTS pvl_currencies (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT);

-- Table: pvl_expenses
DROP TABLE IF EXISTS pvl_expenses;
CREATE TABLE IF NOT EXISTS "pvl_expenses" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_account INTEGER REFERENCES pvl_accounts_current (id), amount REAL, name TEXT, date NUMERIC, description TEXT, id_category INTEGER REFERENCES pvl_expenses_categories (id), id_currency INTEGER REFERENCES pvl_currencies (id));

-- Table: pvl_expenses_categories
DROP TABLE IF EXISTS pvl_expenses_categories;
CREATE TABLE IF NOT EXISTS "pvl_expenses_categories" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT, id_category INTEGER REFERENCES pvl_expenses_categories (id));

-- Table: pvl_expenses_planed
DROP TABLE IF EXISTS pvl_expenses_planed;
CREATE TABLE IF NOT EXISTS "pvl_expenses_planed" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_account INTEGER REFERENCES pvl_accounts_current (id), amount REAL, name TEXT, description TEXT, date INTEGER, status INTEGER, id_currency INTEGER REFERENCES pvl_currencies (id));

-- Table: pvl_plugins
DROP TABLE IF EXISTS pvl_plugins;
CREATE TABLE IF NOT EXISTS pvl_plugins (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, enabled INTEGER, path TEXT);

-- Table: pvl_settings
DROP TABLE IF EXISTS pvl_settings;
CREATE TABLE IF NOT EXISTS "pvl_settings" (id INTEGER PRIMARY KEY, db_version TEXT, instance_name TEXT, lang TEXT, default_currency INTEGER REFERENCES pvl_currencies (id), demo_mode INTEGER, theme TEXT);

-- Table: pvl_users
DROP TABLE IF EXISTS pvl_users;
CREATE TABLE IF NOT EXISTS "pvl_users" (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE, firstname TEXT, lastname TEXT, displayname TEXT, is_admin INTEGER, password TEXT);

COMMIT TRANSACTION;
PRAGMA foreign_keys = on;
