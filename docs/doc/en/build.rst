Build, install and run
======================

Build from source
-----------------

The supported release target is Linux x86-64. Use a C++17 compiler with static
C and C++ runtimes, binutils, CMake 3.18 or later, Git, pkg-config, and development
packages for SQLite, OpenSSL and JsonCpp. The static build requires
``libsqlite3.a`` and ``libcrypto.a``. On Debian, the Docker build uses
``ca-certificates cmake g++ git libjsoncpp-dev libsqlite3-dev libssl-dev make
pkg-config``. Add Python 3.8 or later for HTTP integration tests. When CMake
finds a supported Chromium-based browser, CTest also exercises the browser
file parsers.

Run these commands from the root of the source checkout.

.. code-block:: sh

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ./build/pvledger -c src/config/config.yaml

CMake uses compatible installed libraries or fetches the pinned Oat++ 1.3.0,
yaml-cpp 0.8.0 and JsonCpp 1.9.5 sources as needed. It also builds miniz 3.0.2.
Set ``CMAKE_PREFIX_PATH`` to locate static libraries in a custom prefix.
For a dynamic development build, configure with ``-DPVLEDGER_STATIC=OFF``.
The static build verifies that the final ELF has no interpreter or shared-library
dependencies.

Open `http://127.0.0.1:8080/setup <http://127.0.0.1:8080/setup>`_ and follow
:doc:`guide`. The sample configuration creates its database in
``src/config/data/``. For a manual installation, run
``sudo cmake --install build --component PVLedger``. The default installation
prefix is ``/opt/pvledger``. The executable goes under ``bin/`` and the assets
under ``share/pvledger/``. Set ``CMAKE_INSTALL_PREFIX`` at configure time to
choose a different prefix.

Native packages
---------------

Install `nFPM <https://nfpm.goreleaser.com/docs/install/>`_ on the build machine.
Keep the static release configuration and the ``/opt/pvledger`` prefix.

.. code-block:: sh

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPVLEDGER_STATIC=ON -DCMAKE_INSTALL_PREFIX=/opt/pvledger
   cmake --build build --parallel
   cmake --build build --target packages

Find the artifacts in ``build/packages/``. Build one format with
``package-deb``, ``package-rpm`` or ``package-archlinux``. Set
``PVLEDGER_NFPM`` to the executable path if it is outside ``PATH``.
Install the generated file with ``sudo apt install ./FILE.deb``,
``sudo dnf install ./FILE.rpm`` or ``sudo pacman -U ./FILE.pkg.tar.zst``,
substituting the actual filename.

The packages create a ``pvledger`` account and store instance files in
``/opt/pvledger/var/``. The service reads ``config.yaml`` there and defaults
to ``data/pvledger.sqlite`` within that directory. Start the service explicitly
with one service manager. For systemd, run the commands below.

.. code-block:: sh

   sudo systemctl enable --now pvledger.service
   sudo systemctl status pvledger.service

On a SysV init host, use ``sudo /etc/init.d/pvledger start`` and
``sudo /etc/init.d/pvledger status``. Inspect logs with
``journalctl -u pvledger.service`` on systemd or read
``/opt/pvledger/var/pvledger.log`` on SysV. After an upgrade, restart the
service explicitly. Packages retain the instance files during upgrades and
removal.

Docker Compose
--------------

Run these commands from the source checkout.

.. code-block:: sh

   docker compose up --build -d
   docker compose logs -f pvledger

Open http://127.0.0.1:8080/setup. Compose publishes port 8080 on host
loopback and persists configuration and the database in the
``pvledger-data`` named volume mounted at ``/var/lib/pvledger``.
``docker compose down`` stops the stack and retains that volume.
