Сборка, установка и запуск
==========================

Сборка из исходников
--------------------

Этот раздел описывает сборку для Linux x86-64. Для сборки нужны компилятор
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

Установка в Windows
-------------------

Запустите установщик NSIS в Windows 10 или 11 x64. Путь по умолчанию —
``C:\pvledger``. Для установки и удаления нужны права администратора.
Обычные пользователи получают право чтения и запуска установленных файлов.
В меню «Пуск» появятся ярлыки **PVLedger** и **Uninstall PVLedger**.

Запустите **PVLedger** из меню «Пуск». Значок появится в области уведомлений
или среди скрытых значков. Пункт **Open PVLedger** запускает сервер при
необходимости и открывает приложение в браузере по умолчанию, когда сервер
готов. При первом запуске настройте новый экземпляр или восстановите
резервную копию по инструкции :doc:`guide`. Пункты **Start PVLedger** и
**Stop PVLedger** запускают и останавливают сервер. **About** показывает
версию. **Exit** закрывает помощник и останавливает запущенный им сервер.

Пункт **Configuration** открывает редактор YAML. **Apply** проверяет и
сохраняет изменения, оставляя окно открытым. **OK** сохраняет изменения и
закрывает окно. **Cancel** отменяет изменения после последнего сохранения.
Новые настройки вступают в силу при следующем запуске сервера.
Относительные пути отсчитываются от каталога конфигурации.
По умолчанию используются следующие файлы.

* ``%LOCALAPPDATA%\PVLedger\config.yaml``
* ``%LOCALAPPDATA%\PVLedger\data\pvledger.sqlite``

Перед обновлением или удалением закройте помощник через **Exit**. Удалить
приложение можно через меню «Пуск» или раздел приложений в параметрах
Windows. Обновление и удаление сохраняют файлы пользователя в Local AppData.
Если папка установки изменилась, проверьте сохранённые пути к языковым
пакетам, плагинам и темам.

Сборка для Windows
------------------

Цель CMake ``package-windows`` собирает ``pvledger.exe`` и
``pvledger-win-helper.exe``, упаковывает их и ресурсы приложения в
``build/windows/packages/pvledger-0.1.2-windows-x64-setup.exe``.
На машине сборки требуется NSIS 3.x. CMake ищет ``makensis`` в типовых
каталогах Windows и в ``PATH``. Другой путь можно передать параметром
``-DPVLEDGER_MAKENSIS=/path/to/makensis``.

В Windows установите Visual Studio 2022 с компонентом «Разработка
классических приложений на C++» и Windows SDK, а также CMake, Git и
Python 3.8 или новее. Выполните в PowerShell из каталога исходного кода.

.. code-block:: powershell

   git clone --depth 1 --branch 2025.10.17 https://github.com/microsoft/vcpkg.git "$env:TEMP\pvledger-vcpkg"
   $vcpkg = "$env:TEMP\pvledger-vcpkg"
   & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics
   & "$vcpkg\vcpkg.exe" install --triplet x64-windows-static sqlite3 openssl jsoncpp
   cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DVCPKG_TARGET_TRIPLET=x64-windows-static "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake"
   cmake --build build/windows --config Release --target package-windows
   ctest --test-dir build/windows -C Release --output-on-failure

Чтобы собрать в среде Visual Studio, откройте ``build/windows/PVLedger.sln``,
выберите конфигурацию Release x64 и соберите цель ``package-windows``.

В Debian или Ubuntu установите пакеты ``build-essential cmake git curl zip
unzip tar perl ninja-build nsis g++-mingw-w64-x86-64-posix``. Для Perl
требуется модуль ``IPC::Cmd``. Убедитесь, что имена
``x86_64-w64-mingw32-gcc`` и ``x86_64-w64-mingw32-g++`` в ``PATH``
соответствуют компиляторам с поддержкой POSIX-потоков. Если по умолчанию
выбраны версии Win32, добавьте символьные ссылки на компиляторы
``-posix`` в отдельный каталог в начале ``PATH``.

.. code-block:: sh

   git clone --depth 1 --branch 2025.10.17 https://github.com/microsoft/vcpkg.git /tmp/pvledger-vcpkg
   vcpkg=/tmp/pvledger-vcpkg
   "$vcpkg/bootstrap-vcpkg.sh" -disableMetrics
   "$vcpkg/vcpkg" install --triplet x64-mingw-static sqlite3 openssl jsoncpp
   cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" \
     -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$vcpkg/scripts/toolchains/mingw.cmake" \
     -DVCPKG_TARGET_ARCHITECTURE=x64 -DVCPKG_TARGET_TRIPLET=x64-mingw-static
   cmake --build build/windows --target package-windows

Готовый пакет можно проверить установкой в Windows. CI проверяет
зависимости сборки MinGW-w64, запускает CTest для нативной сборки, проверяет
установку, права доступа и удаление.

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
