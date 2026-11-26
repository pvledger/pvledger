Build, install and run
======================

Build from source
-----------------

This section covers Linux x86-64. Use a C++17 compiler with static
C and C++ runtimes, binutils, CMake 3.18 or later, Git, pkg-config, and development
packages for SQLite 3.35 or newer, OpenSSL and JsonCpp. The static build requires
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

Windows installation
--------------------

Run the NSIS installer on Windows 10 or 11 x64. The default path is
``C:\pvledger``. Installation and removal require elevation. The installer
gives standard users read and execute access to the installed files and adds
**PVLedger** and **Uninstall PVLedger** to the Start menu.

Launch **PVLedger** from the Start menu. Its icon appears in the Windows
notification area, possibly in the overflow. Open the icon menu to use
**Open PVLedger**, **Start PVLedger** or **Stop PVLedger**, **Configuration**,
**About**, and **Exit**. **Open PVLedger** starts the server if needed and
waits until it can open the default browser. On first use, follow :doc:`guide`
to set up a new instance or restore a backup. **Exit** also stops the server
started by the helper.

**Configuration** opens the YAML editor. **Apply** validates and saves the
file and keeps the editor open. **OK** saves and closes it. **Cancel**
discards edits made since the last save. Saved settings take effect at the
next server start. Relative paths resolve from the configuration directory.
The helper uses these files by default.

* ``%LOCALAPPDATA%\PVLedger\config.yaml``
* ``%LOCALAPPDATA%\PVLedger\data\pvledger.sqlite``

Exit the helper before updating or uninstalling. Remove PVLedger from the
Start menu or from **Settings → Apps**. Updates and removal preserve each
user's files in Local AppData. If you reinstall to a different location,
update any saved language, plugin and theme paths that still point to the old
installation.

Windows builds
--------------

Both toolchains build ``pvledger.exe`` and ``pvledger-win-helper.exe``. The
``package-windows`` target stages these binaries and the runtime assets,
then creates ``build/windows/packages/pvledger-0.1.2-windows-x64-setup.exe``.
Install NSIS 3.x on the build machine. CMake searches common Windows locations
and ``PATH``. Use ``-DPVLEDGER_MAKENSIS=/path/to/makensis`` to override it.

On Windows, install Visual Studio 2022 with **Desktop development with C++**
and the Windows SDK, plus CMake, Git and Python 3.8 or later. Run these
PowerShell commands from the source checkout.

.. code-block:: powershell

   git clone --depth 1 --branch 2025.10.17 https://github.com/microsoft/vcpkg.git "$env:TEMP\pvledger-vcpkg"
   $vcpkg = "$env:TEMP\pvledger-vcpkg"
   & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics
   & "$vcpkg\vcpkg.exe" install --triplet x64-windows-static sqlite3 openssl jsoncpp
   cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DVCPKG_TARGET_TRIPLET=x64-windows-static "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake"
   cmake --build build/windows --config Release --target package-windows
   ctest --test-dir build/windows -C Release --output-on-failure

CMake also generates ``build/windows/PVLedger.sln``. Select the x64 Release
configuration and build ``package-windows`` in Visual Studio.

On Debian or Ubuntu, install CMake, Git, curl, zip, unzip, tar, Perl (with
``IPC::Cmd``), native build tools, Ninja, NSIS and the MinGW-w64 x64 POSIX
compiler and binutils. For example, install ``build-essential cmake git curl
zip unzip tar perl ninja-build nsis g++-mingw-w64-x86-64-posix``. Ensure the
unversioned ``x86_64-w64-mingw32-gcc`` and ``x86_64-w64-mingw32-g++`` on
``PATH`` resolve to the POSIX variants. If your system selects the Win32
variants by default, put symlinks to the ``-posix`` binaries in a separate
directory at the front of ``PATH``.

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

Install the resulting package on Windows to validate startup and setup.
CI verifies the x64 MinGW binaries' DLL imports, runs the native CTest suite,
and checks installation, permissions and removal.

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
