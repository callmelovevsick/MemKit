#include "Memory.hpp"

namespace mem {
namespace {

bool iequals(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    return _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

} 

bool Memory::attach(std::wstring_view processName, DWORD access) noexcept {
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

bool Memory::attach(DWORD processId, DWORD access) noexcept {
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

bool Memory::attach(HANDLE existingHandle) noexcept {
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

void Memory::detach() noexcept {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
    processName_.clear();
}

std::optional<Module> Memory::getModule(std::wstring_view moduleName) const noexcept {
    if (!pid_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return std::nullopt;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snapshot == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return std::nullopt;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    std::optional<Module> result;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (iequals(moduleName, entry.szModule)) {
                result.emplace(entry);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (!result) lastError_ = ERROR_NOT_FOUND;
    return result;
}

Module Memory::module(std::wstring_view moduleName) const noexcept {
    auto found = getModule(moduleName);
    return found ? *found : Module{};
}

std::vector<Module> Memory::modules() const noexcept {
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

std::vector<ProcessEntry> Memory::processes() noexcept {
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

bool Memory::readRaw(uintptr_t address, void* buffer, size_t size) const noexcept {
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

bool Memory::writeRaw(uintptr_t address, const void* buffer, size_t size) const noexcept {
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

std::vector<uint8_t> Memory::readBytes(uintptr_t address, size_t size) const noexcept {
    std::vector<uint8_t> buffer(size);
    if (!readRaw(address, buffer.data(), size)) {
        buffer.clear();
    }
    return buffer;
}

bool Memory::writeBytes(uintptr_t address, std::span<const uint8_t> bytes) const noexcept {
    return writeRaw(address, bytes.data(), bytes.size());
}

std::optional<std::string> Memory::readString(uintptr_t address, size_t maxLength) const noexcept {
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

std::optional<std::wstring> Memory::readStringW(uintptr_t address, size_t maxLength) const noexcept {
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

bool Memory::writeString(uintptr_t address, std::string_view text) const noexcept {
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    return writeRaw(address, buffer.data(), buffer.size());
}

bool Memory::protect(uintptr_t address, size_t size, DWORD newProtect, DWORD* oldProtect) noexcept {
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

uintptr_t Memory::allocate(size_t size, DWORD protectFlags) noexcept {
    if (!handle_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return 0;
    }

    LPVOID address = VirtualAllocEx(handle_, nullptr, size, MEM_COMMIT | MEM_RESERVE, protectFlags);
    if (!address) lastError_ = GetLastError();
    return reinterpret_cast<uintptr_t>(address);
}

bool Memory::free(uintptr_t address) noexcept {
    if (!handle_) {
        lastError_ = ERROR_INVALID_HANDLE;
        return false;
    }

    const BOOL ok = VirtualFreeEx(handle_, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE);
    if (!ok) lastError_ = GetLastError();
    return ok != 0;
}

} // namespace mem