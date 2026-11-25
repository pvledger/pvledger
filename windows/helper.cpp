#include "pvledger.hpp"
#include "windows_runtime.hpp"
#include "resource.h"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace {
namespace fs = std::filesystem;
using pvl::win::Handle;
using pvl::win::check;

constexpr wchar_t trayClass[] = L"PVLedger.WinHelper";
constexpr UINT trayMessage = WM_APP + 1;
constexpr UINT_PTR serverTimer = 1;
constexpr DWORD stopTimeout = 5000;
constexpr ULONGLONG startupTimeout = 20000;
enum MenuItem { openLedger = 2001, toggleServer, configure, about, exitHelper };

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const auto length = static_cast<int>(text.size());
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    check(size != 0, "Cannot read UTF-8 text");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), size);
    return result;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
    check(size != 0, "Cannot encode the configuration as UTF-8");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

void showError(HWND owner, const std::exception& error) {
    try {
        MessageBoxW(owner, widen(error.what()).c_str(), L"PVLedger", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxA(owner, error.what(), "PVLedger", MB_OK | MB_ICONERROR);
    }
}

fs::path configPath() {
    PWSTR raw = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> directory(raw, CoTaskMemFree);
    if (FAILED(status)) throw std::runtime_error("Cannot locate Local AppData");
    const auto application = fs::path(directory.get()) / "PVLedger";
    fs::create_directories(application);
    return application / "config.yaml";
}

std::optional<std::string> configText(const fs::path& config) {
    if (!fs::exists(config)) return std::nullopt;
    return pvl::readFile(config);
}

std::wstring editText(const std::string& yaml) {
    std::wstring display;
    for (const auto ch : widen(yaml)) {
        if (ch == L'\r') continue;
        if (ch == L'\n') display += L'\r';
        display += ch;
    }
    return display;
}

struct ProcessAttributes {
    std::vector<unsigned char> buffer;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

    ProcessAttributes() {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        check(size != 0, "Cannot size the child process attributes");
        buffer.resize(size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        check(InitializeProcThreadAttributeList(list, 1, 0, &size), "Cannot prepare the child process attributes");
    }

    ~ProcessAttributes() { DeleteProcThreadAttributeList(list); }
};

class ServerProcess {
    Handle job, process, stopEvent, startupPipe;
    ULONGLONG deadline = 0;
    bool openOnReady = false;
    std::wstring url;

    void clear() {
        startupPipe.reset();
        stopEvent.reset();
        job.reset();
        process.reset();
        openOnReady = false;
        url.clear();
    }

    static void openBrowser(const std::wstring& address) {
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) throw std::runtime_error("Cannot open the default browser");
    }

public:
    ~ServerProcess() { stop(); }

    bool active() const { return process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT; }

    void start(const fs::path& config) {
        clear();
        const auto binary = pvl::win::executablePath().parent_path() / "pvledger.exe";

        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle stop(CreateEventW(&inherit, TRUE, FALSE, nullptr));
        check(bool(stop), "Cannot create the server shutdown event");
        HANDLE reader = nullptr, writer = nullptr;
        check(CreatePipe(&reader, &writer, &inherit, 0), "Cannot create the server startup pipe");
        Handle readPipe(reader), writePipe(writer);
        check(SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0), "Cannot configure the startup pipe");

        Handle newJob(CreateJobObjectW(nullptr, nullptr));
        check(bool(newJob), "Cannot create the server job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(newJob.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
              "Cannot configure the server job");

        ProcessAttributes attributes;
        HANDLE inherited[] = {stop.get(), writePipe.get()};
        check(UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inherited, sizeof(inherited), nullptr, nullptr),
              "Cannot configure inherited server handles");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes.list;
        PROCESS_INFORMATION child{};
        auto command = L"\"" + binary.wstring() + L"\" -c \"" + config.wstring() + L"\" " +
                       pvl::win::helperHandles + L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(stop.get())) +
                       L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(writePipe.get()));
        check(CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, config.parent_path().c_str(), &startup.StartupInfo, &child),
              "Cannot launch pvledger.exe");
        Handle childProcess(child.hProcess), childThread(child.hThread);
        if (!AssignProcessToJobObject(newJob.get(), childProcess.get())) {
            const auto error = GetLastError();
            TerminateProcess(childProcess.get(), 1);
            SetLastError(error);
            check(false, "Cannot supervise pvledger.exe");
        }
        check(ResumeThread(childThread.get()) != DWORD(-1), "Cannot start pvledger.exe");
        job = std::move(newJob);
        process = std::move(childProcess);
        stopEvent = std::move(stop);
        startupPipe = std::move(readPipe);
        deadline = GetTickCount64() + startupTimeout;
    }

    void stop() {
        if (!process) return;
        SetEvent(stopEvent.get());
        if (WaitForSingleObject(process.get(), stopTimeout) != WAIT_OBJECT_0) {
            job.reset();
            WaitForSingleObject(process.get(), 1000);
        }
        clear();
    }

    void open(const fs::path& config) {
        if (!active()) start(config);
        if (url.empty()) openOnReady = true;
        else openBrowser(url);
    }

    void poll() {
        if (!process) return;
        if (startupPipe) {
            DWORD available = 0;
            if (PeekNamedPipe(startupPipe.get(), nullptr, 0, nullptr, &available, nullptr) && available) {
                std::array<char, 4096> buffer{};
                DWORD count = 0;
                check(ReadFile(startupPipe.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr),
                      "Cannot read the server startup response");
                startupPipe.reset();
                if (count < 3 || buffer[count - 1] != '\n')
                    throw std::runtime_error("Invalid server startup response");
                const std::string response(buffer.data() + 1, count - 2);
                if (buffer[0] == 'E') {
                    clear();
                    throw std::runtime_error("PVLedger startup failed.\n\n" + response);
                }
                if (buffer[0] != 'R') throw std::runtime_error("Invalid server startup response");
                const auto colon = response.rfind(':');
                const auto host = response.substr(0, colon);
                const auto port = colon == std::string::npos ? "" : response.substr(colon + 1);
                if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos ||
                    port.empty() || port.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("The listening address cannot be opened in a browser");
                const auto browserHost = host == "0.0.0.0" || host == "localhost" ? "127.0.0.1" : host;
                url = widen("http://" + browserHost + ":" + port + "/");
                if (openOnReady) {
                    openOnReady = false;
                    openBrowser(url);
                }
                return;
            }
            if (GetTickCount64() >= deadline) {
                stop();
                throw std::runtime_error("PVLedger did not start within 20 seconds");
            }
        }
        if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
            clear();
            throw std::runtime_error("PVLedger exited unexpectedly");
        }
    }
};

class TrayApp {
    HINSTANCE instance;
    HWND window = nullptr;
    HWND dialog = nullptr;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    NOTIFYICONDATAW tray{};
    fs::path config;
    std::optional<std::string> original;
    std::wstring shown;
    ServerProcess server;

    void addIcon() {
        if (!Shell_NotifyIconW(NIM_ADD, &tray)) throw std::runtime_error("Cannot add the PVLedger tray icon");
        tray.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &tray))
            tray.uVersion = 0;
    }

    void showMenu() {
        const HMENU menu = CreatePopupMenu();
        check(menu != nullptr, "Cannot create the tray menu");
        POINT position{};
        GetCursorPos(&position);
        AppendMenuW(menu, MF_STRING, openLedger, L"Open PVLedger");
        AppendMenuW(menu, MF_STRING, toggleServer, server.active() ? L"Stop PVLedger" : L"Start PVLedger");
        AppendMenuW(menu, MF_STRING, configure, L"Configuration\u2026");
        AppendMenuW(menu, MF_STRING, about, L"About");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, exitHelper, L"Exit");
        SetMenuDefaultItem(menu, openLedger, FALSE);
        SetForegroundWindow(window);
        const auto action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                           position.x, position.y, 0, window, nullptr);
        DestroyMenu(menu);
        PostMessageW(window, WM_NULL, 0, 0);
        if (action) SendMessageW(window, WM_COMMAND, action, 0);
    }

    void showConfig() {
        if (!dialog) {
            original = configText(config);
            shown = editText(original ? *original : pvl::Config::load(config).toYaml());
            dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CONFIGURATION), window, dialogProc,
                                        reinterpret_cast<LPARAM>(this));
            check(dialog != nullptr, "Cannot open the configuration dialog");
        }
        ShowWindow(dialog, SW_SHOW);
        SetForegroundWindow(dialog);
    }

    void saveConfig(HWND editor) {
        const HWND field = GetDlgItem(editor, IDC_YAML);
        const int length = GetWindowTextLengthW(field);
        std::wstring text(length + 1, L'\0');
        text.resize(GetWindowTextW(field, text.data(), length + 1));
        if (text == shown && original) return;
        auto yaml = narrow(text);
        yaml.erase(std::remove(yaml.begin(), yaml.end(), '\r'), yaml.end());
        const auto validated = pvl::Config::load(config, &yaml);
        if (configText(config) != original)
            throw std::runtime_error("config.yaml changed while the editor was open. Close and reopen Configuration to reload it.");
        validated.saveYaml(yaml);
        original = std::move(yaml);
        shown = std::move(text);
    }

    static INT_PTR CALLBACK dialogProc(HWND editor, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_INITDIALOG) {
            auto* app = reinterpret_cast<TrayApp*>(lparam);
            SetWindowLongPtrW(editor, DWLP_USER, reinterpret_cast<LONG_PTR>(app));
            SetDlgItemTextW(editor, IDC_CONFIG_PATH, app->config.c_str());
            const HWND field = GetDlgItem(editor, IDC_YAML);
            SendMessageW(field, EM_SETLIMITTEXT, 0, 0);
            SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
            SetWindowTextW(field, app->shown.c_str());
            SetFocus(field);
            return FALSE;
        }
        auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(editor, DWLP_USER));
        if (!app) return FALSE;
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
            DestroyWindow(editor);
            return TRUE;
        }
        if (message == WM_DESTROY) {
            app->dialog = nullptr;
            return TRUE;
        }
        if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDC_APPLY)) {
            try {
                app->saveConfig(editor);
                if (LOWORD(wparam) == IDOK) DestroyWindow(editor);
            } catch (const std::exception& error) {
                showError(editor, error);
            }
            return TRUE;
        }
        return FALSE;
    }

    LRESULT dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case trayMessage:
                if (LOWORD(lparam) == WM_CONTEXTMENU || (!tray.uVersion && lparam == WM_RBUTTONUP)) showMenu();
                if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT ||
                    (!tray.uVersion && lparam == WM_LBUTTONUP))
                    server.open(config);
                return 0;
            case WM_COMMAND:
                switch (LOWORD(wparam)) {
                    case openLedger: server.open(config); break;
                    case toggleServer:
                        if (server.active()) server.stop();
                        else server.start(config);
                        break;
                    case configure: showConfig(); break;
                    case about:
                        MessageBoxW(window, widen("PVLedger " PVL_VERSION "\n\nManage your finances privately").c_str(),
                                    L"About PVLedger", MB_OK | MB_ICONINFORMATION);
                        break;
                    case exitHelper: DestroyWindow(window); break;
                }
                return 0;
            case WM_TIMER:
                if (wparam == serverTimer) server.poll();
                return 0;
            case WM_ENDSESSION:
                if (wparam) server.stop();
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd, serverTimer);
                Shell_NotifyIconW(NIM_DELETE, &tray);
                server.stop();
                window = nullptr;
                PostQuitMessage(0);
                return 0;
            default:
                if (message == taskbarCreated) {
                    addIcon();
                    return 0;
                }
                return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!app) return DefWindowProcW(hwnd, message, wparam, lparam);
        try {
            return app->dispatch(hwnd, message, wparam, lparam);
        } catch (const std::exception& error) {
            showError(app->dialog ? app->dialog : hwnd, error);
            return 0;
        }
    }

public:
    explicit TrayApp(HINSTANCE module) : instance(module), config(configPath()) {}

    ~TrayApp() {
        if (window) DestroyWindow(window);
    }

    int run() {
        const auto icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PVLEDGER));
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = windowProc;
        type.hInstance = instance;
        type.hIcon = icon;
        type.lpszClassName = trayClass;
        check(RegisterClassExW(&type) != 0, "Cannot register the PVLedger tray window");
        window = CreateWindowExW(WS_EX_TOOLWINDOW, trayClass, L"PVLedger", WS_OVERLAPPED, 0, 0, 0, 0,
                                 nullptr, nullptr, instance, this);
        check(window != nullptr, "Cannot create the PVLedger tray window");
        tray.cbSize = sizeof(tray);
        tray.hWnd = window;
        tray.uID = 1;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray.uCallbackMessage = trayMessage;
        tray.hIcon = icon;
        lstrcpyW(tray.szTip, L"PVLedger");
        addIcon();
        check(SetTimer(window, serverTimer, 100, nullptr) != 0, "Cannot monitor the server");

        MSG message{};
        BOOL status;
        while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (dialog && IsDialogMessageW(dialog, &message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        check(status != -1, "Cannot read the PVLedger window messages");
        return static_cast<int>(message.wParam);
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        Handle singleInstance(CreateMutexW(nullptr, FALSE, trayClass));
        const auto status = GetLastError();
        check(bool(singleInstance), "Cannot create the PVLedger helper mutex");
        if (status == ERROR_ALREADY_EXISTS) return 0;
        return TrayApp(instance).run();
    } catch (const std::exception& error) {
        showError(nullptr, error);
        return 1;
    }
}
