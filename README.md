# MemKit

Thanks @cheat_and_math for the upgrade im building upon ur library
(`I made it easier to manipulate with commands.`)

A small, modern C++ (C++20) wrapper around the Win32 process-memory APIs
(`ReadProcessMemory`, `WriteProcessMemory`, `VirtualAllocEx`, `Toolhelp32`, ...).

It replaces long, C-style calls like

```cpp
uintptr_t addr = mem.pointer_resolve(base, offsets, 3);
```

with a short, chainable, autocomplete-friendly API:

```cpp
int hp = memory.pointer(base).offset(0x10).offset(0x20).offset(0x8).read<int>();
```

> **Scope & responsible use.** This library reads and writes the memory of another
> process. Only point it at processes you own or are otherwise authorized to
> inspect or modify — for example your own applications, single-player/offline
> games you own, or software you are debugging/reverse-engineering under a
> license or agreement that permits it. Attaching to arbitrary third-party or
> multiplayer processes may violate that software's terms of service or
> applicable law; that is on the person using the library, not the library
> itself.

---

## Features

- **Chainable pointer resolution** — `mem.pointer(base).offset(0x10)...read<T>()`
- **Typed read/write** — `read<T>()`, `tryRead<T>()` (returns `std::optional`), `write()`
- **Strings & raw bytes** — `readString`, `readStringW`, `writeString`, `readBytes`, `writeBytes`
- **Module lookup** — `module()`, `getModule()`, `modules()`
- **Process enumeration** — `Memory::processes()` (static, no attach required)
- **RAII everywhere** — the process handle closes itself; `MemoryProtectionGuard`
  restores the original page protection automatically
- **`Protection` enum class** as a typed alternative to raw `PAGE_*` flags
- **Move-only, non-copyable `Memory`** — no accidental double-`CloseHandle`
- **All Win32 calls check their return value** and record the last error via `lastError()`
- Header + source split (`Memory.hpp` / `Memory.cpp`), single namespace: `mem`

---

## What changed vs. the original API

| Before | Problem | After |
|---|---|---|
| `attach(name, access)` only | No way to attach by PID or existing handle | `attach(name)`, `attach(pid)`, `attach(handle)` overloads |
| `read_memory<T>()` / `write_memory<T>()` | Long names, no failure signal | `read<T>()`, `tryRead<T>()` (`std::optional`), `write<T>()` |
| `get_module_infor()` (typo), returns `{}` on failure | Hard to check for failure, name has a typo | `getModule()` → `std::optional<Module>`, plus a convenience `module()` |
| `pointer_resolve(base, offset[], size)` | C-array + size, not chainable | `pointer(base).offset(a).offset(b)....read<T>()` |
| `change_memory_proctection()` (typo) | Never restores the old protection → permanently weakens memory protection | `protect()` restores on request, and `scopedProtect()` restores **automatically** via RAII |
| `free_mem()` | Awkward name to avoid clashing with `::free` | `free()` — no clash, since it's a class member |
| No error reporting anywhere | Silent failures (`ReadProcessMemory` return value ignored, etc.) | Every call checks its return value and updates `lastError()` |
| `CreateToolhelp32Snapshot` / `Process32FirstW` return values never checked | Could dereference an uninitialized buffer if the snapshot is empty/fails | All snapshot calls now check `INVALID_HANDLE_VALUE` and the first enumeration call |
| Handles never closed on early return paths | Handle leaks | RAII (`~Memory`, `MemoryProtectionGuard`) + consistent cleanup |
| Raw `DWORD` protection flags only | Easy to typo a `PAGE_*` constant | `enum class Protection` overloads alongside the raw `DWORD` ones (both work) |

---

## Installation

MemKit is two files — drop them into your project:

```
include/Memory.hpp
src/Memory.cpp
```

Requirements:
- Windows, C++20 (uses `std::span`)
- MSVC 2019 16.10+, or MinGW-w64 GCC 10+
- Links against `kernel32` only (already linked by default in a normal Win32 project)

CMake example:

```cmake
add_library(memkit STATIC src/Memory.cpp)
target_include_directories(memkit PUBLIC include)
target_compile_features(memkit PUBLIC cxx_std_20)
target_link_libraries(your_target PRIVATE memkit)
```

---

## Quick Start

```cpp
#include "Memory.hpp"

int main() {
    mem::Memory memory;

    if (!memory.attach(L"game.exe")) {
        return 1; // process not found, or OpenProcess failed — check memory.lastError()
    }

    uintptr_t base = memory.module(L"client.dll").base();

    int hp = memory.pointer(base)
                   .offset(0x10)
                   .offset(0x20)
                   .offset(0x8)
                   .read<int>();

    memory.write(base + 0x100, 999);
}
```

---

## Attach Process

```cpp
mem::Memory memory;

memory.attach(L"game.exe");                       // by process name
memory.attach(L"game.exe", PROCESS_VM_READ);       // by name, with a custom access mask
memory.attach(1234u);                              // by PID
memory.attach(someExistingHandle);                 // by an already-open HANDLE (Memory takes ownership)

memory.isAttached();   // bool
memory.pid();          // DWORD
memory.handle();       // HANDLE
memory.processName();  // std::wstring

memory.detach();       // closes the handle; also happens automatically in ~Memory()
```

`PROCESS_ALL_ACCESS` is the default access mask (same default as the original
library). For production code, prefer requesting only what you need, e.g.
`PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION`.

---

## Read Memory

```cpp
int hp        = memory.read<int>(addr);         // returns T{} on failure
float speed   = memory.read<float>(addr);
Vec3 position = memory.read<Vec3>(addr);         // any trivially copyable type

std::optional<int> maybeHp = memory.tryRead<int>(addr); // std::nullopt on failure

std::vector<uint8_t> bytes = memory.readBytes(addr, 64);

std::optional<std::string>  name  = memory.readString(addr, 64);   // ANSI, null-terminated
std::optional<std::wstring> wname = memory.readStringW(addr, 64);  // UTF-16, null-terminated
```

## Write Memory

```cpp
memory.write(addr, 999);
memory.write(addr, 3.14f);
memory.write(addr, Vec3{1.0f, 2.0f, 3.0f});   // returns bool

memory.writeBytes(addr, bytes);               // std::span<const uint8_t>
memory.writeString(addr, "hello");            // writes the bytes + a null terminator
```

`read<T>` / `write<T>` are restricted to trivially copyable types at compile
time (`static_assert`), matching how `ReadProcessMemory`/`WriteProcessMemory`
actually behave — they copy raw bytes.

---

## Pointer Chain

`pointer(base)` mirrors the original `pointer_resolve()` exactly: each
`.offset(x)` call **dereferences the current address, then adds `x`**. That
means `base` itself must point to a pointer (e.g. `module_base + static_offset`),
same as before.

```cpp
// Old:
uintptr_t offsets[] = { 0x10, 0x20, 0x8 };
uintptr_t addr = mem.pointer_resolve(base, offsets, 3);
int hp = mem.read_memory<int>(addr);

// New:
int hp = memory.pointer(base)
               .offset(0x10)
               .offset(0x20)
               .offset(0x8)
               .read<int>();

// Or, if you need the raw address instead of a value:
uintptr_t addr = memory.pointer(base).offset(0x10).offset(0x20).offset(0x8).address();

// If any dereference along the chain hits 0, the chain becomes invalid
// and short-circuits (matching the old "return 0" behavior):
auto p = memory.pointer(base).offset(0x10).offset(0x20);
if (!p) {
    // one of the intermediate pointers was null
}
```

---

## Module

```cpp
mem::Module client = memory.module(L"client.dll");  // never throws; check .valid()
if (client) {
    uintptr_t base = client.base();
    size_t     size = client.size();
    std::wstring name = client.name();
    std::wstring path = client.path();
}

// Or, if you want an explicit "found / not found" signal:
if (auto found = memory.getModule(L"client.dll")) {
    uintptr_t base = found->base();
}

std::vector<mem::Module> allModules = memory.modules();
```

---

## Allocate Memory

```cpp
uintptr_t buffer = memory.allocate(256);                                  // PAGE_EXECUTE_READWRITE (default, matches old behavior)
uintptr_t buffer2 = memory.allocate(256, mem::Protection::ReadWrite);     // typed overload
uintptr_t buffer3 = memory.allocate(256, PAGE_READWRITE);                 // raw DWORD overload
```

## Free Memory

```cpp
memory.free(buffer);
```

## Change Protection

```cpp
// One-shot, you restore it yourself:
DWORD oldProtect = 0;
memory.protect(addr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect);
// ... do the write ...
memory.protect(addr, sizeof(int), oldProtect); // restore manually

// RAII version — restores automatically, even if an exception/early return happens:
{
    auto guard = memory.scopedProtect(addr, sizeof(int), PAGE_EXECUTE_READWRITE);
    memory.write(addr, 999);
} // original protection restored here

// Or keep the new protection permanently:
auto guard = memory.scopedProtect(addr, sizeof(int), PAGE_EXECUTE_READWRITE);
guard.dismiss(); // destructor will no longer restore it
```

---

## API Reference

### `mem::Memory`

| Method | Description |
|---|---|
| `attach(std::wstring_view name, DWORD access = PROCESS_ALL_ACCESS)` | Finds a running process by executable name and opens it. |
| `attach(DWORD pid, DWORD access = PROCESS_ALL_ACCESS)` | Opens a process directly by PID. |
| `attach(HANDLE handle)` | Adopts an already-open handle; `Memory` takes ownership. |
| `detach()` | Closes the handle and resets state. Safe to call multiple times. |
| `isAttached()` | Whether a process handle is currently held. |
| `pid()`, `handle()`, `processName()` | Basic identity accessors. |
| `getModule(name)` | Returns `std::optional<Module>`. |
| `module(name)` | Convenience version: returns a default (`.valid() == false`) `Module` instead of `std::nullopt`. |
| `modules()` | All modules loaded in the attached process. |
| `static processes()` | Lists all running processes (pid + name); does not require `attach()`. |
| `read<T>(addr)` | Reads a trivially copyable `T`. Returns `T{}` on failure. |
| `tryRead<T>(addr)` | Same, but returns `std::optional<T>` so you can detect failure. |
| `write<T>(addr, value)` | Writes a trivially copyable `T`. Returns `bool`. |
| `readBytes(addr, size)` / `writeBytes(addr, span)` | Raw byte access. |
| `readString` / `readStringW` / `writeString` | Null-terminated ANSI/UTF-16 string helpers. |
| `dereference(addr)` | Reads a `uintptr_t` at `addr` (used internally by `Pointer`). |
| `pointer(base)` | Starts a chainable `Pointer` at `base`. |
| `protect(addr, size, newProtect, old = nullptr)` | Wraps `VirtualProtectEx`; accepts raw `DWORD` or `Protection`. |
| `scopedProtect(addr, size, newProtect)` | Same, but returns a `MemoryProtectionGuard` that restores on scope exit. |
| `allocate(size, protect = PAGE_EXECUTE_READWRITE)` | Wraps `VirtualAllocEx`; accepts raw `DWORD` or `Protection`. |
| `free(addr)` | Wraps `VirtualFreeEx`. |
| `lastError()` | Win32 error code (`GetLastError()`) of the last failed call. |

### `mem::Pointer`

| Method | Description |
|---|---|
| `offset(off)` | Dereferences the current address, adds `off`, returns `*this`. |
| `address()` | The current resolved address. |
| `valid()` / `operator bool()` | `false` if any dereference in the chain hit 0. |
| `read<T>()` / `tryRead<T>()` / `write<T>(value)` | Same semantics as on `Memory`, applied at `address()`. |

### `mem::Module`

`name()`, `path()`, `base()`, `size()`, `valid()` / `operator bool()`.

### `mem::MemoryProtectionGuard`

`restore()` (idempotent), `dismiss()` (keep the new protection permanently), `active()`.

### `mem::Protection` (enum class)

`NoAccess`, `ReadOnly`, `ReadWrite`, `Execute`, `ExecuteRead`, `ExecuteReadWrite` — thin,
typed aliases for the corresponding `PAGE_*` constants (`toWin32()` converts back to `DWORD`).

---

## Examples

**Read an int**
```cpp
int ammo = memory.read<int>(addr);
```

**Read a float**
```cpp
float speed = memory.read<float>(addr);
```

**Read a struct**
```cpp
struct Vec3 { float x, y, z; };
Vec3 position = memory.read<Vec3>(addr);
```

**Read a string**
```cpp
if (auto name = memory.readString(addr, 32)) {
    std::cout << *name << "\n";
}
```

**Pointer chain**
```cpp
int hp = memory.pointer(base).offset(0x10).offset(0x20).offset(0x8).read<int>();
```

**Module base**
```cpp
uintptr_t base = memory.module(L"client.dll").base();
```

**Allocate + write + free**
```cpp
uintptr_t buf = memory.allocate(64, mem::Protection::ReadWrite);
memory.writeBytes(buf, someBytes);
memory.free(buf);
```

**Change protection (RAII)**
```cpp
{
    auto guard = memory.scopedProtect(addr, sizeof(int), PAGE_EXECUTE_READWRITE);
    memory.write(addr, 1234);
}
```

**Full example**
```cpp
#include "Memory.hpp"
#include <iostream>

int main() {
    mem::Memory memory;
    if (!memory.attach(L"game.exe")) {
        std::cerr << "Attach failed, error " << memory.lastError() << "\n";
        return 1;
    }

    uintptr_t base = memory.module(L"client.dll").base();
    if (!base) {
        std::cerr << "Module not found\n";
        return 1;
    }

    int hp = memory.pointer(base)
                   .offset(0x10)
                   .offset(0x20)
                   .offset(0x8)
                   .read<int>();

    std::cout << "HP: " << hp << "\n";

    memory.detach();
}
```
