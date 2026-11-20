--
-- File generated with Letos v4.0.4 on Fri Nov 20 16:47:45 2026
--
-- Text encoding used: UTF-8
--
PRAGMA foreign_keys = off;
BEGIN TRANSACTION;

-- Table: vpl_access_rights
DROP TABLE IF EXISTS vpl_access_rights;
CREATE TABLE IF NOT EXISTS "vpl_access_rights" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT);

-- Table: vpl_accounts_credit
DROP TABLE IF EXISTS vpl_accounts_credit;
CREATE TABLE IF NOT EXISTS "vpl_accounts_credit" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES vpl_banks (id), id_user INTEGER REFERENCES vpl_users (id), name TEXT, description TEXT, interest REAL, billing_date INTEGER, ammount REAL, id_current_account INTEGER REFERENCES vpl_accounts_current (id), id_currency INTEGER REFERENCES vpl_currencies (id));

-- Table: vpl_accounts_current
DROP TABLE IF EXISTS vpl_accounts_current;
CREATE TABLE IF NOT EXISTS "vpl_accounts_current" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES vpl_banks (id), name TEXT, amount REAL, description TEXT, id_user INTEGER REFERENCES vpl_users (id), id_currency INTEGER REFERENCES vpl_currencies (id));

-- Table: vpl_accounts_savings
DROP TABLE IF EXISTS vpl_accounts_savings;
CREATE TABLE IF NOT EXISTS "vpl_accounts_savings" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_bank INTEGER REFERENCES vpl_banks (id), interest REAL, billing_date NUMERIC, name TEXT, description TEXT, id_user REFERENCES vpl_users (id), ammount REAL, id_currency INTEGER REFERENCES vpl_currencies (id));

-- Table: vpl_banks
DROP TABLE IF EXISTS vpl_banks;
CREATE TABLE IF NOT EXISTS "vpl_banks" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT, id_contries INTEGER REFERENCES vpl_countries (id));
INSERT INTO vpl_banks (id, name, description, id_contries) VALUES (0, 'T-Bank', 'Former Tinkoff Bank', 0);

-- Table: vpl_countries
DROP TABLE IF EXISTS vpl_countries;
CREATE TABLE IF NOT EXISTS "vpl_countries" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, iso REAL);
INSERT INTO vpl_countries (id, name, iso) VALUES (0, 'United States Of America', 'US');

-- Table: vpl_currencies
DROP TABLE IF EXISTS vpl_currencies;
CREATE TABLE IF NOT EXISTS vpl_currencies (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT);

-- Table: vpl_expenses
DROP TABLE IF EXISTS vpl_expenses;
CREATE TABLE IF NOT EXISTS "vpl_expenses" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_account INTEGER REFERENCES vpl_accounts_current (id), ammount REAL, name TEXT, date NUMERIC, description, id_category INTEGER REFERENCES vpl_expenses_categories (id), id_currency INTEGER REFERENCES vpl_currencies (id));

-- Table: vpl_expenses_categories
DROP TABLE IF EXISTS vpl_expenses_categories;
CREATE TABLE IF NOT EXISTS "vpl_expenses_categories" (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, description TEXT, id_category INTEGER REFERENCES vpl_expenses_categories (id));

-- Table: vpl_expenses_planed
DROP TABLE IF EXISTS vpl_expenses_planed;
CREATE TABLE IF NOT EXISTS "vpl_expenses_planed" (id INTEGER PRIMARY KEY AUTOINCREMENT, id_account INTEGER REFERENCES vpl_accounts_current (id), ammount REAL, name TEXT, description TEXT, date INTEGER, status INTEGER, id_currency INTEGER REFERENCES vpl_currencies (id));

-- Table: vpl_plugins
DROP TABLE IF EXISTS vpl_plugins;
CREATE TABLE IF NOT EXISTS vpl_plugins (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, enabled INTEGER, path TEXT);

-- Table: vpl_settings
DROP TABLE IF EXISTS vpl_settings;
CREATE TABLE IF NOT EXISTS "vpl_settings" (db_version TEXT, instance_name TEXT, lang TEXT, default_currency INTEGER REFERENCES vpl_currencies (id), demo_mode INTEGER, theme TEXT);

-- Table: vpl_users
DROP TABLE IF EXISTS vpl_users;
CREATE TABLE IF NOT EXISTS "vpl_users" (id INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE, username TEXT UNIQUE, firstname TEXT, lastname TEXT, displayname TEXT, is_admin INTEGER, password TEXT);

COMMIT TRANSACTION;
PRAGMA foreign_keys = on;
