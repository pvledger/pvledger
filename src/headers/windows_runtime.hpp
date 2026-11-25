#pragma once

#include <windows.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace pvl::win {
struct CloseHandleOnExit {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandleOnExit>;

inline void check(bool success, const char* message) {
    if (!success) throw std::system_error(GetLastError(), std::system_category(), message);
}

inline std::filesystem::path executablePath() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    check(length != 0 && length < path.size(), "Cannot locate the executable");
    path.resize(length);
    return path;
}

inline constexpr wchar_t helperHandles[] = L"--windows-helper-handles";
}
