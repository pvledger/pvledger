Reference and links
===================

Configuration
-------------

Relative paths are resolved from the directory containing the YAML file.
Run ``pvledger -c /path/to/config.yaml`` to select it. Passing a new filename
in an existing writable directory starts setup with bundled assets and
``data/pvledger.sqlite`` relative to that directory. PVLedger writes the
configuration when setup completes.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - YAML key
     - Meaning
   * - ``app.host``
     - IPv4 bind address. Defaults to ``127.0.0.1``.
   * - ``app.port``
     - TCP port from 1 to 65535. Defaults to 8080.
   * - ``app.secure_cookie``
     - Enables the Secure session cookie for HTTPS deployments.
   * - ``database.engine``
     - Database engine. Use ``sqlite``.
   * - ``database.sqlite.path``
     - Path to the SQLite file.
   * - ``paths.languages``, ``paths.plugins``, ``paths.themes``
     - Asset directories for translations, local plugins and themes.

The :download:`example YAML <../../../src/config/config.yaml>` shows the
layout in the source checkout.

API and specifications
----------------------

The JSON API uses ``/api/v1``. Session authentication uses a cookie and
mutating requests require a CSRF token as described in the API contract.

* :download:`API contract (Markdown) <../../api/README.md>`
* :download:`OpenAPI definition (YAML) <../../api/openapi.yaml>`
* :download:`Application specification (Markdown) <../../spec/pvledger_application_spec.md>`
* :download:`Database specification (Markdown) <../../spec/pvledger_database_spec.md>`
* :download:`Database specification (JSON) <../../spec/pvledger_database_spec.json>`
* :download:`Sample plugin manifest (YAML) <../../../src/plugins/sample/plugin.yaml>`

Project
-------

* `PVLedger source repository <https://forge.pvadimr.com/pvledger/pvledger>`_
* :download:`Project license <../../../LICENSE>`
* `Maintainer and commercial licensing <mailto:pvadimr@pvadimr.com>`_
