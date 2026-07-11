# mem-kit

A small C++20 library for reading and writing the memory of another Windows
process. Two files, no dependencies beyond the Windows SDK: `mem.hpp` and
`mem.cpp`. Drop them into a Visual Studio project and you're done.

It wraps `ReadProcessMemory`, `WriteProcessMemory`, `VirtualAllocEx`,
`VirtualProtectEx` and the Toolhelp32 snapshot APIs behind an interface built
around one class, `mem::Process`, plus two small helpers, `mem::Pointer` and
`mem::Module`.

```cpp
#include "mem.hpp"
using namespace mem;

Process game;
game.attach(L"PlantsVsZombies.exe");

int sun = game.read<int>(0x1B0F6740);
game.write(0x1B0F6740, 9999);
```

Only point this at processes you own or are otherwise authorized to inspect
or modify: your own software, offline/single-player games you own, or
anything you're debugging under a license or agreement that allows it.
Attaching to third-party or multiplayer processes you don't control may
violate that software's terms of service, and that responsibility sits with
whoever uses the library, not with the library itself.

## Requirements

- Windows
- C++20 (the library uses `std::span`)
- MSVC 2019 16.10 or newer, or MinGW-w64 GCC 10 or newer
- Links against `kernel32`, which any normal Win32 project already links

Build your project for the same architecture as the process you're
attaching to. A 64-bit build cannot correctly read pointers out of a 32-bit
process, and the reverse doesn't work either. `uintptr_t` is sized to match
your own build, not the target's.

## Installation

Copy `mem.hpp` and `mem.cpp` into your project and add `mem.cpp` to your
build. That's the entire installation step. There's no CMake file, no
package manager entry, no `include/` folder to configure.

## Quick Start

```cpp
#include "mem.hpp"
using namespace mem;

int main() {
    Process game;
    if (!game.attach(L"PlantsVsZombies.exe")) {
        return 1;
    }

    int sun = game.read<int>(0x1B0F6740);
    game.write(0x1B0F6740, 9999);

    game.detach();
}
```

## Namespace

Everything lives in `mem`. `using namespace mem;` is fine in a `.cpp` file
that only deals with process memory; in a header, prefer qualifying names
(`mem::Process`) to avoid dragging the whole namespace into whatever
includes it.

## Basic Usage

The object you interact with is `Process`. It represents an attached
target and exposes every operation the library provides: attaching,
reading, writing, resolving pointers, scanning for byte patterns,
allocating memory, and changing page protection.

```cpp
Process game;
game.attach(L"game.exe");

int health = game.read<int>(addr);
float speed = game.read<float>(addr + 0x4);
```

## Attach Process

```cpp
Process game;

game.attach(L"game.exe");
game.attach(L"game.exe", PROCESS_VM_READ);
game.attach(1234u);
game.attach(someExistingHandle);
```

`attach` has three overloads:

- `attach(std::wstring_view name, DWORD access = PROCESS_ALL_ACCESS)` looks
  through the running processes for an exact (case-insensitive) match on the
  executable name and opens it.
- `attach(DWORD pid, DWORD access = PROCESS_ALL_ACCESS)` opens a process
  directly by process ID, skipping the name lookup.
- `attach(HANDLE handle)` adopts a handle you already opened yourself.
  `Process` takes ownership of it and will close it on `detach()` or when
  the `Process` object is destroyed.

All three return `bool`. `false` means the process wasn't found or
`OpenProcess` failed (insufficient privileges is the usual cause); check
`lastError()` for the Win32 error code.

`detach()` closes the handle and resets the object to an unattached state.
It's safe to call more than once and runs automatically in the destructor,
so a `Process` going out of scope always cleans up after itself. `isAttached()`
tells you whether a handle is currently held. `pid()`, `handle()`, and
`processName()` return the attached process's ID, raw handle, and resolved
executable name.

## Read Memory

```cpp
int ammo = game.read<int>(addr);
float hp = game.read<float>(addr);

struct Vec3 { float x, y, z; };
Vec3 position = game.read<Vec3>(addr);

std::optional<int> maybeAmmo = game.tryRead<int>(addr);

std::vector<int> waveIds = game.readArray<int>(addr, 20);

std::vector<uint8_t> raw = game.readBytes(addr, 64);

std::optional<std::string> name = game.readString(addr, 32);
std::optional<std::wstring> wname = game.readWString(addr, 32);
```

`read<T>(address)` copies `sizeof(T)` bytes from the target process and
returns them as a `T`. `T` must be trivially copyable, since the underlying
call is a raw memory copy; the compiler enforces this with a `static_assert`.
On failure it returns a default-constructed `T{}`, which is convenient but
indistinguishable from a real zero value. When that distinction matters, use
`tryRead<T>(address)` instead, which returns `std::optional<T>` and comes
back empty on failure.

`readArray<T>(address, count)` reads `count` contiguous elements of `T` and
returns them as a `std::vector<T>`. On failure the vector is empty, so check
`.empty()` before indexing.

`readBytes(address, size)` reads a raw block of memory into a
`std::vector<uint8_t>`, useful for structures you don't want to model with a
C++ type, or as input to `scan()`.

`readString`/`readWString` read a null-terminated ANSI or UTF-16 string, up
to `maxLength` characters, and return `std::nullopt` if the read fails.
They stop at the first null terminator they find inside that window; if the
string is longer than `maxLength`, it's truncated.

## Write Memory

```cpp
game.write(addr, 9999);
game.write(addr, 3.14f);
game.write(addr, Vec3{1.0f, 2.0f, 3.0f});

game.writeArray<int>(addr, waveIds);
game.writeBytes(addr, raw);
game.writeString(addr, "hello");
```

`write<T>(address, value)` and `writeArray<T>(address, span)` mirror their
read counterparts and return `bool`. `writeString` writes the bytes of the
string plus a trailing null terminator, so make sure the destination buffer
in the target process is big enough to hold it.

## Pointer Chain

Games and most non-trivial applications store data behind multiple levels
of indirection: a static module offset that holds a pointer, which holds
another pointer, and so on, until the last hop lands on the actual value.
`pointer()` walks that chain for you.

```cpp
uintptr_t base = game.module(L"client.dll").base();

int hp = game.pointer(base)
             .offset(0x10)
             .offset(0x20)
             .offset(0x8)
             .read<int>();
```

Each `.offset(x)` call dereferences the *current* address (reads the
pointer stored there) and adds `x` to the result. That means the value you
start the chain with has to itself be a location that holds a pointer,
typically a module base plus a static offset, not a final data address. If
any dereference along the way returns 0 (a null pointer, or a failed read),
the chain becomes invalid and every following call becomes a cheap no-op
that returns `T{}` or `std::nullopt` rather than crashing.

`.address()` gives you the raw resolved address if you need it for
something other than a typed read. `.valid()` / `explicit operator bool()`
tell you whether the chain resolved successfully. `.write<T>(value)` writes
through the resolved pointer, the same way `.read<T>()` reads through it.

## Module

```cpp
Module client = game.module(L"client.dll");
if (client) {
    uintptr_t base = client.base();
    size_t size = client.size();
    std::wstring name = client.name();
    std::wstring path = client.path();
}

std::vector<Module> all = game.modules();
```

`module(name)` looks up a loaded module by name (case-insensitive) and
returns a `Module`. If the module isn't found, you get back a default,
invalid `Module`, not an exception or a null pointer, check `.valid()` or
just use the object in a boolean context (`if (client)`) before trusting
`base()`. `modules()` returns every module currently loaded in the attached
process.

## Pattern Scan

Hardcoded addresses break the moment the target application updates.
Pattern scanning finds a byte sequence, including wildcard bytes, inside
the process's memory, so you can locate code or data by its signature
instead of a fixed address.

```cpp
auto addr = game.scan(L"client.dll", "48 8B 05 ?? ?? ?? ?? 48 85 C0");
if (addr) {
    uintptr_t functionStart = *addr;
}

auto addr2 = game.scan(base, moduleSize, "8B 44 24 08 ?? 90");
```

The pattern format is space-separated hex bytes, with `?` or `??` standing
in for a byte that can be anything (the same format Cheat Engine and most
disassemblers use for AOB/IDA-style signatures). `scan(moduleName, pattern)`
searches an entire module; `scan(start, size, pattern)` searches an
arbitrary address range, which is what the module overload calls
internally after resolving the module's base and size. Both return
`std::optional<uintptr_t>`, empty if nothing matched.

Internally, the scan walks the target's memory region by region using
`VirtualQueryEx`, skipping anything that isn't committed or is marked
`PAGE_NOACCESS`/`PAGE_GUARD`, so it won't fail outright just because part of
a module's address range happens to be unmapped.

## Allocation

```cpp
uintptr_t buffer = game.allocate(256);
uintptr_t buffer2 = game.allocate(256, Protection::ReadWrite);
uintptr_t buffer3 = game.allocate(256, PAGE_READWRITE);

game.free(buffer);
```

`allocate(size, protect)` wraps `VirtualAllocEx` with `MEM_COMMIT |
MEM_RESERVE` and returns the address of the new region, or 0 on failure.
The default protection is `PAGE_EXECUTE_READWRITE`, matching the original
library's behavior; pass a narrower value if you don't need executable
memory. `free(address)` releases memory previously returned by `allocate()`.

## Protection

```cpp
DWORD oldProtect = 0;
game.protect(addr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect);
game.write(addr, 9999);
game.restore(addr, sizeof(int), oldProtect);
```

`protect(address, size, newProtect, oldProtect = nullptr)` wraps
`VirtualProtectEx`. If `oldProtect` is non-null, the previous protection
value is written there so you can put it back later. `restore(address,
size, oldProtect)` is that "put it back" call, it's `protect()` under a
name that makes the intent at the call site obvious. There's a `Protection`
overload of `protect()` too, if you'd rather write `Protection::ReadWrite`
than remember the matching `PAGE_*` constant.

The library does not restore protection automatically. If you change it,
pair the call with `restore()` yourself, ideally right after the write you
needed the new protection for.

## API Reference

### `mem::Process`

| Function | Parameters | Returns | Notes |
|---|---|---|---|
| `attach(name, access = PROCESS_ALL_ACCESS)` | `name`: executable file name to search for. `access`: desired `OpenProcess` access mask. | `bool` | Finds the process by name and opens it. |
| `attach(pid, access = PROCESS_ALL_ACCESS)` | `pid`: process ID. `access`: access mask. | `bool` | Opens a process directly, no name lookup. |
| `attach(handle)` | `handle`: an already-open `HANDLE`. | `bool` | Adopts the handle; `Process` now owns it. |
| `detach()` | none | `void` | Closes the handle. Safe to call repeatedly. |
| `isAttached()` | none | `bool` | Whether a handle is currently held. |
| `pid()` | none | `DWORD` | Attached process ID, 0 if not attached. |
| `handle()` | none | `HANDLE` | The raw handle, for calling Win32 APIs the library doesn't wrap. |
| `processName()` | none | `const std::wstring&` | Executable name resolved at attach time. |
| `module(name)` | `name`: module file name. | `Module` | Invalid (`.valid() == false`) if not found. |
| `modules()` | none | `std::vector<Module>` | All modules loaded in the process. |
| `static processes()` | none | `std::vector<ProcessEntry>` | Every running process; doesn't require `attach()`. |
| `read<T>(address)` | `address`: source address. | `T` | `T{}` on failure. `T` must be trivially copyable. |
| `tryRead<T>(address)` | `address`: source address. | `std::optional<T>` | `std::nullopt` on failure. |
| `write<T>(address, value)` | `address`: destination. `value`: data to write. | `bool` | |
| `readArray<T>(address, count)` | `address`: source. `count`: element count. | `std::vector<T>` | Empty vector on failure. |
| `writeArray<T>(address, values)` | `address`: destination. `values`: a `std::span<const T>`. | `bool` | |
| `readBytes(address, size)` | `address`, `size` in bytes. | `std::vector<uint8_t>` | Empty on failure. |
| `writeBytes(address, bytes)` | `address`. `bytes`: `std::span<const uint8_t>`. | `bool` | |
| `readString(address, maxLength = 256)` | `address`. `maxLength`: character cap. | `std::optional<std::string>` | ANSI, stops at the first null byte. |
| `readWString(address, maxLength = 256)` | same | `std::optional<std::wstring>` | UTF-16 version. |
| `writeString(address, text)` | `address`. `text`: `std::string_view`. | `bool` | Writes the bytes plus a null terminator. |
| `dereference(address)` | `address` | `uintptr_t` | Reads a `uintptr_t` at `address`; what `Pointer::offset` uses internally. |
| `pointer(base)` | `base`: starting address. | `Pointer` | Begins a chain; see Pointer Chain above. |
| `scan(moduleName, pattern)` | `moduleName`. `pattern`: AOB string. | `std::optional<uintptr_t>` | Searches one module. |
| `scan(start, size, pattern)` | `start`, `size`, `pattern`. | `std::optional<uintptr_t>` | Searches an arbitrary range. |
| `protect(address, size, newProtect, oldProtect = nullptr)` | `newProtect`: raw `DWORD` or `Protection`. `oldProtect`: optional out-param. | `bool` | Wraps `VirtualProtectEx`. |
| `restore(address, size, oldProtect)` | `oldProtect`: value captured from `protect()`. | `bool` | Puts the previous protection back. |
| `allocate(size, protect = PAGE_EXECUTE_READWRITE)` | `size` in bytes. `protect`: raw `DWORD` or `Protection`. | `uintptr_t` | 0 on failure. |
| `free(address)` | `address` from `allocate()`. | `bool` | Wraps `VirtualFreeEx`. |
| `lastError()` | none | `DWORD` | `GetLastError()` value from the most recent failed call. |

### `mem::Pointer`

Returned by `Process::pointer(base)`. Not constructed directly.

| Function | Returns | Notes |
|---|---|---|
| `offset(value)` | `Pointer&` | Dereferences the current address, adds `value`. Chainable. |
| `address()` | `uintptr_t` | The address resolved so far. |
| `valid()` / `operator bool()` | `bool` | `false` once any dereference in the chain hits 0. |
| `read<T>()` | `T` | Reads at the resolved address; `T{}` if the chain is invalid. |
| `tryRead<T>()` | `std::optional<T>` | Same, with failure signaled through `std::nullopt`. |
| `write<T>(value)` | `bool` | Writes at the resolved address. |

### `mem::Module`

| Function | Returns |
|---|---|
| `name()` | `const std::wstring&` |
| `path()` | `const std::wstring&` |
| `base()` | `uintptr_t` |
| `size()` | `size_t` |
| `valid()` / `operator bool()` | `bool` |

### `mem::Protection`

`enum class Protection : DWORD` with values `NoAccess`, `ReadOnly`,
`ReadWrite`, `Execute`, `ExecuteRead`, `ExecuteReadWrite`, typed aliases for
the matching `PAGE_*` constants. `toWin32(Protection)` converts one back to
a raw `DWORD`, in case you need to pass it to a Win32 call the library
doesn't wrap.

### `mem::ProcessEntry`

A plain struct: `DWORD pid` and `std::wstring name`, one entry per running
process, returned by `Process::processes()`.

## Examples

**Read an int, a float, and a struct**
```cpp
int ammo = game.read<int>(addr);
float speed = game.read<float>(addr + 0x4);

struct Vec3 { float x, y, z; };
Vec3 pos = game.read<Vec3>(addr + 0x8);
```

**Read a string safely**
```cpp
if (auto name = game.readString(addr, 32)) {
    std::cout << *name << "\n";
} else {
    std::cout << "read failed, error " << game.lastError() << "\n";
}
```

**Resolve a pointer chain and read through it**
```cpp
uintptr_t base = game.module(L"client.dll").base();
int hp = game.pointer(base).offset(0x10).offset(0x20).offset(0x8).read<int>();
```

**Find a function by pattern and call through it**
```cpp
if (auto addr = game.scan(L"client.dll", "55 8B EC 83 EC ?? 53 56 57")) {
    uintptr_t functionAddress = *addr;
}
```

**Allocate a buffer, write to it, free it**
```cpp
uintptr_t buffer = game.allocate(64, Protection::ReadWrite);
if (buffer) {
    game.writeBytes(buffer, someBytes);
    game.free(buffer);
}
```

**Change protection, write, restore**
```cpp
DWORD oldProtect = 0;
if (game.protect(addr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect)) {
    game.write(addr, 1234);
    game.restore(addr, sizeof(int), oldProtect);
}
```

**A complete program**
```cpp
#include "mem.hpp"
#include <iostream>

int main() {
    mem::Process game;
    if (!game.attach(L"PlantsVsZombies.exe")) {
        std::cerr << "attach failed, error " << game.lastError() << "\n";
        return 1;
    }

    int sun = game.read<int>(0x1B0F6740);
    std::cout << "sun: " << sun << "\n";
    game.write(0x1B0F6740, 9999);

    game.detach();
}
```

## Best Practices

- Prefer `tryRead<T>` over `read<T>` when a zero value would be a
  legitimate reading and you need to tell that apart from a failed read.
- Check `module()`/`Pointer::valid()` before trusting `base()` or a
  resolved address; both fail quietly by design instead of throwing.
- Request the narrowest `OpenProcess` access mask that covers what you
  actually do. `PROCESS_ALL_ACCESS` is the default for compatibility with
  the original library, but `PROCESS_VM_READ | PROCESS_VM_WRITE |
  PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION` is usually enough and
  is more likely to succeed without administrator rights.
- Always pair `protect()` with `restore()`. The library won't do it for
  you, and leaving a page more permissive than it needs to be is the kind
  of thing that's easy to forget and annoying to debug later.
- Build for the same bitness (x86 or x64) as your target process.
- Re-run `scan()` after the target updates. That's the point of using a
  pattern instead of a hardcoded address, but the pattern itself can still
  go stale if the surrounding code changes shape entirely.

## Common Mistakes

- Treating `read<T>()` returning `0` as proof the read failed. It might
  have, or the value might just be zero. Use `tryRead<T>()` if it matters.
- Forgetting that `pointer(base)` expects `base` to point *at* a pointer,
  not at the final value. If your first offset looks wrong, you're
  probably one dereference short or one too many.
- Writing a pattern without spaces (`"488B05????"` instead of
  `"48 8B 05 ?? ?? ?? ??"`). The parser splits on spaces; a pattern with no
  spaces is read as a single, invalid token.
- Calling `write()`/`allocate()` with `PAGE_EXECUTE_READWRITE` out of habit
  when the memory doesn't need to be executable. It works, but it's a
  wider permission than most writes need.
- Mixing up a 32-bit target with a 64-bit build (or the reverse). Pointers
  will read back truncated or garbage, and it won't necessarily fail
  loudly.

# Credits

This project was inspired by the educational videos and Windows memory
programming tutorials from Cheat and Math.

Special thanks to Cheat and Math for sharing valuable knowledge with the
programming community.

YouTube:
https://www.youtube.com/@cheat_and_math

This project is an independent redesign and extension built upon the
concepts learned from those tutorials.
