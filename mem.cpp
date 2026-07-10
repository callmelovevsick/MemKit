#include "mem.hpp"

namespace mem {
namespace {

bool iequals(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    return _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

std::vector<std::optional<uint8_t>> parsePattern(std::string_view pattern) noexcept {
    std::vector<std::optional<uint8_t>> tokens;
    size_t i = 0;
    while (i < pattern.size()) {
        while (i < pattern.size() && pattern[i] == ' ') ++i;
        if (i >= pattern.size()) break;
        size_t start = i;
        while (i < pattern.size() && pattern[i] != ' ') ++i;
        std::string_view token = pattern.substr(start, i - start);

        if (token.empty()) continue;
        if (token.find_first_not_of('?') == std::string_view::npos) {
            tokens.push_back(std::nullopt);
            continue;
        }

        unsigned long value = 0;
        for (char c : token) {
            unsigned digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
            else return {};
            value = value * 16 + digit;
        }
        tokens.push_back(static_cast<uint8_t>(value));
    }
    return tokens;
}

std::optional<size_t> findPattern(std::span<const uint8_t> data, const std::vector<std::optional<uint8_t>>& tokens) noexcept {
    if (tokens.empty() || data.size() < tokens.size()) return std::nullopt;
    for (size_t i = 0; i + tokens.size() <= data.size(); ++i) {
        bool matches = true;
        for (size_t j = 0; j < tokens.size(); ++j) {
            if (tokens[j] && data[i + j] != *tokens[j]) {
                matches = false;
                break;
            }
        }
        if (matches) return i;
    }
    return std::nullopt;
}

}

bool Process::attach(std::wstring_view processName, DWORD access) noexcept {
    detach();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD foundPid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (iequals(processName, entry.szExeFile)) {
                foundPid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (foundPid == 0) {
        lastError_ = ERROR_NOT_FOUND;
        return false;
    }

    return attach(foundPid, access);
}

bool Process::attach(DWORD processId, DWORD access) noexcept {
    detach();

    HANDLE opened = OpenProcess(access, FALSE, processId);
    if (!opened) {
        lastError_ = GetLastError();
        return false;
    }

    handle_ = opened;
    pid_ = processId;
    processName_.clear();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == processId) {
                    processName_ = entry.szExeFile;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    return true;
}

bool Process::attach(HANDLE existingHandle) noexcept {
    detach();

    if (!existingHandle || existingHandle == INVALID_HANDLE_VALUE) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    handle_ = existingHandle;
    pid_ = GetProcessId(existingHandle);
    processName_.clear();
    return true;
}

void Process::detach() noexcept {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
    processName_.clear();
}

Module Process::module(std::wstring_view moduleName) const noexcept {
    if (!pid_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return {};
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snapshot == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return {};
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    Module result;
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (iequals(moduleName, entry.szModule)) {
                result = Module(entry);
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (!found) lastError_ = ERROR_NOT_FOUND;
    return result;
}

std::vector<Module> Process::modules() const noexcept {
    std::vector<Module> result;
    if (!pid_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return result;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snapshot == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return result;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            result.emplace_back(entry);
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::vector<ProcessEntry> Process::processes() noexcept {
    std::vector<ProcessEntry> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            result.push_back(ProcessEntry{entry.th32ProcessID, entry.szExeFile});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool Process::readRaw(uintptr_t address, void* buffer, size_t size) const noexcept {
    if (!handle_ || !address || !buffer) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    SIZE_T bytesRead = 0;
    const BOOL ok = ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead);
    if (!ok || bytesRead != size) {
        lastError_ = GetLastError();
        return false;
    }
    return true;
}

bool Process::writeRaw(uintptr_t address, const void* buffer, size_t size) const noexcept {
    if (!handle_ || !address || !buffer) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    SIZE_T bytesWritten = 0;
    const BOOL ok = WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(address), buffer, size, &bytesWritten);
    if (!ok || bytesWritten != size) {
        lastError_ = GetLastError();
        return false;
    }
    return true;
}

std::vector<uint8_t> Process::readBytes(uintptr_t address, size_t size) const noexcept {
    std::vector<uint8_t> buffer(size);
    if (!readRaw(address, buffer.data(), size)) buffer.clear();
    return buffer;
}

bool Process::writeBytes(uintptr_t address, std::span<const uint8_t> bytes) const noexcept {
    return writeRaw(address, bytes.data(), bytes.size());
}

std::optional<std::string> Process::readString(uintptr_t address, size_t maxLength) const noexcept {
    if (!handle_ || !address || maxLength == 0) {
        lastError_ = ERROR_INVALID_HANDLE;
        return std::nullopt;
    }

    std::string buffer(maxLength, '\0');
    SIZE_T bytesRead = 0;
    const BOOL ok = ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), buffer.data(), maxLength, &bytesRead);
    if (!ok || bytesRead == 0) {
        lastError_ = GetLastError();
        return std::nullopt;
    }

    const auto nullPos = buffer.find('\0');
    buffer.resize(nullPos == std::string::npos ? bytesRead : nullPos);
    return buffer;
}

std::optional<std::wstring> Process::readWString(uintptr_t address, size_t maxLength) const noexcept {
    if (!handle_ || !address || maxLength == 0) {
        lastError_ = ERROR_INVALID_HANDLE;
        return std::nullopt;
    }

    std::wstring buffer(maxLength, L'\0');
    SIZE_T bytesRead = 0;
    const size_t byteSize = maxLength * sizeof(wchar_t);
    const BOOL ok = ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), buffer.data(), byteSize, &bytesRead);
    if (!ok || bytesRead == 0) {
        lastError_ = GetLastError();
        return std::nullopt;
    }

    const auto nullPos = buffer.find(L'\0');
    buffer.resize(nullPos == std::wstring::npos ? bytesRead / sizeof(wchar_t) : nullPos);
    return buffer;
}

bool Process::writeString(uintptr_t address, std::string_view text) const noexcept {
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    return writeRaw(address, buffer.data(), buffer.size());
}

std::optional<uintptr_t> Process::scan(uintptr_t start, size_t size, std::string_view pattern) const noexcept {
    if (!handle_ || !start || size == 0) return std::nullopt;

    auto tokens = parsePattern(pattern);
    if (tokens.empty()) return std::nullopt;

    uintptr_t cursor = start;
    const uintptr_t end = start + size;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != sizeof(info)) {
            break;
        }

        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        const uintptr_t scanEnd = regionEnd < end ? regionEnd : end;
        const bool committed = info.State == MEM_COMMIT;
        const bool blocked = (info.Protect & PAGE_NOACCESS) != 0 || (info.Protect & PAGE_GUARD) != 0;

        if (committed && !blocked && scanEnd > cursor) {
            auto region = readBytes(cursor, scanEnd - cursor);
            if (auto offset = findPattern(region, tokens)) {
                return cursor + *offset;
            }
        }

        cursor = regionEnd > cursor ? regionEnd : end;
    }

    return std::nullopt;
}

std::optional<uintptr_t> Process::scan(std::wstring_view moduleName, std::string_view pattern) const noexcept {
    Module target = module(moduleName);
    if (!target) return std::nullopt;
    return scan(target.base(), target.size(), pattern);
}

bool Process::protect(uintptr_t address, size_t size, DWORD newProtect, DWORD* oldProtect) noexcept {
    if (!handle_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    DWORD previous = 0;
    const BOOL ok = VirtualProtectEx(handle_, reinterpret_cast<LPVOID>(address), size, newProtect, &previous);
    if (!ok) {
        lastError_ = GetLastError();
        return false;
    }
    if (oldProtect) *oldProtect = previous;
    return true;
}

uintptr_t Process::allocate(size_t size, DWORD protectFlags) noexcept {
    if (!handle_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return 0;
    }

    LPVOID address = VirtualAllocEx(handle_, nullptr, size, MEM_COMMIT | MEM_RESERVE, protectFlags);
    if (!address) lastError_ = GetLastError();
    return reinterpret_cast<uintptr_t>(address);
}

bool Process::free(uintptr_t address) noexcept {
    if (!handle_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    const BOOL ok = VirtualFreeEx(handle_, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE);
    if (!ok) lastError_ = GetLastError();
    return ok != 0;
}

}
