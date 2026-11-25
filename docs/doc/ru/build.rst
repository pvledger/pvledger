Сборка, установка и запуск
==========================

Сборка из исходников
--------------------

Поддерживаемая платформа выпуска — Linux x86-64. Для сборки нужны компилятор
с поддержкой C++17 и статическими библиотеками среды выполнения C и C++,
binutils, CMake 3.18 или новее, Git, pkg-config и пакеты разработки SQLite,
OpenSSL и JsonCpp. Статической сборке нужны ``libsqlite3.a`` и
``libcrypto.a``. В сборочном образе Debian используются пакеты
``ca-certificates cmake g++ git libjsoncpp-dev libsqlite3-dev libssl-dev make
pkg-config``. Для интеграционных HTTP-тестов нужен Python 3.8 или новее.
Если CMake найдёт поддерживаемый браузер на базе Chromium, CTest также
проверит обработку файлов в браузере.

Выполните команды из корня репозитория.

.. code-block:: sh

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ./build/pvledger -c src/config/config.yaml

CMake использует подходящие установленные библиотеки или при необходимости
скачивает исходники Oat++ 1.3.0, yaml-cpp 0.8.0 и JsonCpp 1.9.5. CMake
также собирает miniz 3.0.2. Передайте ``CMAKE_PREFIX_PATH``, если статические
библиотеки установлены в другом каталоге. Для динамической сборки при
разработке укажите ``-DPVLEDGER_STATIC=OFF``. При статической сборке CMake
проверяет, что итоговый ELF не требует загрузчика и разделяемых библиотек.

Откройте `http://127.0.0.1:8080/setup <http://127.0.0.1:8080/setup>`_
и следуйте :doc:`guide`. С конфигурацией из примера база создаётся в
``src/config/data/``. Для ручной установки выполните
``sudo cmake --install build --component PVLedger``. По умолчанию
исполняемый файл попадёт в ``/opt/pvledger/bin/``, а ресурсы —
в ``/opt/pvledger/share/pvledger/``. Чтобы изменить каталог установки,
задайте ``CMAKE_INSTALL_PREFIX`` на этапе конфигурирования CMake.

Пакеты для дистрибутивов
------------------------

Установите `nFPM <https://nfpm.goreleaser.com/docs/install/>`_ на сборочную
машину. Используйте статическую сборку и префикс ``/opt/pvledger``.

.. code-block:: sh

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON -DCMAKE_INSTALL_PREFIX=/opt/pvledger
   cmake --build build --parallel
   cmake --build build --target packages

Пакеты появятся в ``build/packages/``. Для сборки одного формата
используйте цель ``package-deb``, ``package-rpm`` или ``package-archlinux``.
Если nFPM отсутствует в ``PATH``, задайте путь к нему через
``PVLEDGER_NFPM``. Установите пакет командой
``sudo apt install ./FILE.deb``, ``sudo dnf install ./FILE.rpm`` или
``sudo pacman -U ./FILE.pkg.tar.zst``, подставив имя файла.

Пакеты создают системного пользователя ``pvledger``. Служба читает
``/opt/pvledger/var/config.yaml`` и по умолчанию хранит базу в подкаталоге
``data/`` под именем ``pvledger.sqlite``. Запустите службу вручную через
один диспетчер служб. В systemd выполните следующие команды.

.. code-block:: sh

   sudo systemctl enable --now pvledger.service
   sudo systemctl status pvledger.service

В системах с SysV init используйте ``sudo /etc/init.d/pvledger start`` и
``sudo /etc/init.d/pvledger status``. Для просмотра журналов используйте
``journalctl -u pvledger.service`` в systemd или
``/opt/pvledger/var/pvledger.log`` в SysV. После обновления пакета
перезапустите службу вручную. При обновлении и удалении пакетов файлы
экземпляра сохраняются.

Docker Compose
--------------

Выполните команды из корня репозитория.

.. code-block:: sh

   docker compose up --build -d
   docker compose logs -f pvledger

Откройте http://127.0.0.1:8080/setup. Compose публикует порт 8080 на
локальном адресе хоста и хранит конфигурацию и базу в именованном томе
``pvledger-data``, подключённом к ``/var/lib/pvledger``. Команда
``docker compose down`` останавливает контейнер и сохраняет этот том.
