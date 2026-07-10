#pragma once

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mem {

class Process;

enum class Protection : DWORD {
    NoAccess = PAGE_NOACCESS,
    ReadOnly = PAGE_READONLY,
    ReadWrite = PAGE_READWRITE,
    Execute = PAGE_EXECUTE,
    ExecuteRead = PAGE_EXECUTE_READ,
    ExecuteReadWrite = PAGE_EXECUTE_READWRITE,
};

[[nodiscard]] constexpr DWORD toWin32(Protection protection) noexcept {
    return static_cast<DWORD>(protection);
}

struct ProcessEntry {
    DWORD pid = 0;
    std::wstring name;
};

class Module {
public:
    Module() noexcept = default;

    explicit Module(const MODULEENTRY32W& entry) noexcept
        : name_(entry.szModule),
          path_(entry.szExePath),
          base_(reinterpret_cast<uintptr_t>(entry.modBaseAddr)),
          size_(entry.modBaseSize) {}

    [[nodiscard]] const std::wstring& name() const noexcept { return name_; }
    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }
    [[nodiscard]] uintptr_t base() const noexcept { return base_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { return base_ != 0; }
    explicit operator bool() const noexcept { return valid(); }

private:
    std::wstring name_;
    std::wstring path_;
    uintptr_t base_ = 0;
    size_t size_ = 0;
};

class Pointer {
public:
    Pointer(Process& process, uintptr_t address) noexcept
        : process_(&process), address_(address), valid_(address != 0) {}

    Pointer& offset(uintptr_t offset) noexcept;

    [[nodiscard]] uintptr_t address() const noexcept { return address_; }
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    explicit operator bool() const noexcept { return valid_; }

    template <typename T>
    [[nodiscard]] T read() const noexcept;

    template <typename T>
    [[nodiscard]] std::optional<T> tryRead() const noexcept;

    template <typename T>
    bool write(const T& value) const noexcept;

private:
    Process* process_;
    uintptr_t address_;
    bool valid_;
};

class Process {
public:
    Process() noexcept = default;
    ~Process() { detach(); }

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    Process(Process&& other) noexcept { *this = std::move(other); }
    Process& operator=(Process&& other) noexcept;

    bool attach(std::wstring_view processName, DWORD access = PROCESS_ALL_ACCESS) noexcept;
    bool attach(DWORD processId, DWORD access = PROCESS_ALL_ACCESS) noexcept;
    bool attach(HANDLE existingHandle) noexcept;
    void detach() noexcept;
    [[nodiscard]] bool isAttached() const noexcept { return handle_ != nullptr; }

    [[nodiscard]] DWORD pid() const noexcept { return pid_; }
    [[nodiscard]] HANDLE handle() const noexcept { return handle_; }
    [[nodiscard]] const std::wstring& processName() const noexcept { return processName_; }

    [[nodiscard]] Module module(std::wstring_view moduleName) const noexcept;
    [[nodiscard]] std::vector<Module> modules() const noexcept;

    [[nodiscard]] static std::vector<ProcessEntry> processes() noexcept;

    template <typename T>
    [[nodiscard]] T read(uintptr_t address) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        T value{};
        readRaw(address, &value, sizeof(T));
        return value;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> tryRead(uintptr_t address) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        T value{};
        if (!readRaw(address, &value, sizeof(T))) return std::nullopt;
        return value;
    }

    template <typename T>
    bool write(uintptr_t address, const T& value) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return writeRaw(address, &value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] std::vector<T> readArray(uintptr_t address, size_t count) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        std::vector<T> values(count);
        if (!readRaw(address, values.data(), count * sizeof(T))) values.clear();
        return values;
    }

    template <typename T>
    bool writeArray(uintptr_t address, std::span<const T> values) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return writeRaw(address, values.data(), values.size() * sizeof(T));
    }

    [[nodiscard]] std::vector<uint8_t> readBytes(uintptr_t address, size_t size) const noexcept;
    bool writeBytes(uintptr_t address, std::span<const uint8_t> bytes) const noexcept;

    [[nodiscard]] std::optional<std::string> readString(uintptr_t address, size_t maxLength = 256) const noexcept;
    [[nodiscard]] std::optional<std::wstring> readWString(uintptr_t address, size_t maxLength = 256) const noexcept;
    bool writeString(uintptr_t address, std::string_view text) const noexcept;

    [[nodiscard]] uintptr_t dereference(uintptr_t address) const noexcept { return read<uintptr_t>(address); }
    [[nodiscard]] Pointer pointer(uintptr_t base) noexcept { return Pointer(*this, base); }

    [[nodiscard]] std::optional<uintptr_t> scan(uintptr_t start, size_t size, std::string_view pattern) const noexcept;
    [[nodiscard]] std::optional<uintptr_t> scan(std::wstring_view moduleName, std::string_view pattern) const noexcept;

    bool protect(uintptr_t address, size_t size, DWORD newProtect, DWORD* oldProtect = nullptr) noexcept;
    bool protect(uintptr_t address, size_t size, Protection newProtect, DWORD* oldProtect = nullptr) noexcept {
        return protect(address, size, toWin32(newProtect), oldProtect);
    }
    bool restore(uintptr_t address, size_t size, DWORD oldProtect) noexcept {
        return protect(address, size, oldProtect, nullptr);
    }

    [[nodiscard]] uintptr_t allocate(size_t size, DWORD protectFlags = PAGE_EXECUTE_READWRITE) noexcept;
    [[nodiscard]] uintptr_t allocate(size_t size, Protection protectFlags) noexcept {
        return allocate(size, toWin32(protectFlags));
    }
    bool free(uintptr_t address) noexcept;

    [[nodiscard]] DWORD lastError() const noexcept { return lastError_; }

private:
    bool readRaw(uintptr_t address, void* buffer, size_t size) const noexcept;
    bool writeRaw(uintptr_t address, const void* buffer, size_t size) const noexcept;

    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
    std::wstring processName_;
    mutable DWORD lastError_ = 0;
};

inline Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        detach();
        handle_ = other.handle_;
        pid_ = other.pid_;
        processName_ = std::move(other.processName_);
        lastError_ = other.lastError_;
        other.handle_ = nullptr;
        other.pid_ = 0;
    }
    return *this;
}

inline Pointer& Pointer::offset(uintptr_t offset) noexcept {
    if (!valid_) return *this;
    const uintptr_t dereferenced = process_->dereference(address_);
    if (!dereferenced) {
        valid_ = false;
        address_ = 0;
        return *this;
    }
    address_ = dereferenced + offset;
    return *this;
}

template <typename T>
inline T Pointer::read() const noexcept {
    return valid_ ? process_->read<T>(address_) : T{};
}

template <typename T>
inline std::optional<T> Pointer::tryRead() const noexcept {
    if (!valid_) return std::nullopt;
    return process_->tryRead<T>(address_);
}

template <typename T>
inline bool Pointer::write(const T& value) const noexcept {
    return valid_ && process_->write(address_, value);
}

}
