Справочник и ссылки
===================

Конфигурация
------------

Относительные пути отсчитываются от каталога файла YAML. Чтобы выбрать
файл, запустите ``pvledger -c /path/to/config.yaml``. Если указать имя
нового файла в существующем каталоге с правом записи, мастер использует
встроенный комплект ресурсов и путь ``data/pvledger.sqlite`` относительно
этого каталога. PVLedger запишет конфигурацию по завершении настройки.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Ключ YAML
     - Назначение
   * - ``app.host``
     - IPv4-адрес сервера. По умолчанию ``127.0.0.1``.
   * - ``app.port``
     - Порт TCP от 1 до 65535. По умолчанию 8080.
   * - ``app.secure_cookie``
     - Включает атрибут Secure для cookie сеанса при работе через HTTPS.
   * - ``database.engine``
     - Движок базы данных. Укажите ``sqlite``.
   * - ``database.sqlite.path``
     - Путь к файлу SQLite.
   * - ``paths.languages``, ``paths.plugins``, ``paths.themes``
     - Каталоги переводов, локальных расширений и тем оформления.

В :download:`примере YAML <../../../src/config/config.yaml>` указаны пути
для запуска из репозитория.

API и спецификации
------------------

JSON API доступен по префиксу ``/api/v1``. Для сеанса используется cookie,
а запросы, меняющие данные, требуют токен CSRF. Подробности приведены в
контракте API. Документы по ссылкам ниже написаны на английском.

* :download:`Контракт API (Markdown) <../../api/README.md>`
* :download:`Описание OpenAPI (YAML) <../../api/openapi.yaml>`
* :download:`Спецификация приложения (Markdown) <../../spec/pvledger_application_spec.md>`
* :download:`Спецификация базы данных (Markdown) <../../spec/pvledger_database_spec.md>`
* :download:`Спецификация базы данных (JSON) <../../spec/pvledger_database_spec.json>`
* :download:`Пример манифеста расширения (YAML) <../../../src/plugins/sample/plugin.yaml>`

Проект
------

* `Репозиторий PVLedger <https://forge.pvadimr.com/pvledger/pvledger>`_
* :download:`Лицензия проекта (на английском) <../../../LICENSE>`
* `Связь с автором и коммерческое лицензирование <mailto:pvadimr@pvadimr.com>`_
