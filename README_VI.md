# mem

Đây là một thư viện C++20 nhỏ gọn để đọc và ghi bộ nhớ của một tiến trình
(process) khác trên Windows. Chỉ có hai file, không cần cài thêm thư viện
ngoài Windows SDK: `mem.hpp` và `mem.cpp`. Copy hai file này vào project
Visual Studio là dùng được ngay.

Thư viện bọc lại các hàm Win32 quen thuộc như `ReadProcessMemory`,
`WriteProcessMemory`, `VirtualAllocEx`, `VirtualProtectEx` và họ hàm
Toolhelp32, gói gọn trong một class chính là `mem::Process`, cùng hai class
phụ trợ nhỏ là `mem::Pointer` và `mem::Module`.

```cpp
#include "mem.hpp"
using namespace mem;

Process game;
game.attach(L"PlantsVsZombies.exe");

int sun = game.read<int>(0x1B0F6740);
game.write(0x1B0F6740, 9999);
```

Chỉ nên dùng thư viện này trên những tiến trình bạn sở hữu hoặc được phép
can thiệp: phần mềm của chính bạn, game offline/single-player bạn đã mua,
hoặc phần mềm bạn đang debug theo giấy phép/thỏa thuận cho phép làm vậy.
Việc attach vào tiến trình của bên thứ ba hoặc game multiplayer mà bạn
không có quyền can thiệp có thể vi phạm điều khoản dịch vụ của phần mềm
đó, và trách nhiệm này thuộc về người dùng thư viện, không phải bản thân
thư viện.

## Yêu cầu

- Windows
- C++20 (thư viện dùng `std::span`)
- MSVC 2019 16.10 trở lên, hoặc MinGW-w64 GCC 10 trở lên
- Link với `kernel32`, thứ mà bất kỳ project Win32 bình thường nào cũng đã
  link sẵn

Hãy build project của bạn cùng kiến trúc (32-bit hay 64-bit) với tiến trình
bạn định attach vào. Một build 64-bit không thể đọc đúng con trỏ từ một
tiến trình 32-bit, và ngược lại cũng vậy. `uintptr_t` có kích thước theo
build của bạn, không phải theo tiến trình đích.

## Cài đặt

Copy `mem.hpp` và `mem.cpp` vào project, thêm `mem.cpp` vào danh sách file
build. Vậy là xong, không cần file CMake, không cần cấu hình package
manager, không cần tạo thư mục `include/` riêng.

## Bắt đầu nhanh

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

Mọi thứ nằm trong namespace `mem`. Viết `using namespace mem;` trong một
file `.cpp` chỉ xử lý bộ nhớ tiến trình thì không sao; nhưng trong file
header, nên dùng tên đầy đủ (`mem::Process`) để tránh kéo cả namespace vào
mọi nơi include header đó.

## Cách dùng cơ bản

Đối tượng bạn làm việc chính là `Process`. Nó đại diện cho tiến trình đang
attach, và chứa mọi thao tác thư viện cung cấp: attach, đọc, ghi, giải
pointer chain, quét pattern, cấp phát bộ nhớ, và đổi quyền bảo vệ trang
nhớ.

```cpp
Process game;
game.attach(L"game.exe");

int health = game.read<int>(addr);
float speed = game.read<float>(addr + 0x4);
```

## Attach vào Process

```cpp
Process game;

game.attach(L"game.exe");
game.attach(L"game.exe", PROCESS_VM_READ);
game.attach(1234u);
game.attach(someExistingHandle);
```

`attach` có ba overload:

- `attach(std::wstring_view name, DWORD access = PROCESS_ALL_ACCESS)` duyệt
  qua danh sách tiến trình đang chạy, tìm tên file thực thi khớp (không
  phân biệt hoa thường), rồi mở tiến trình đó.
- `attach(DWORD pid, DWORD access = PROCESS_ALL_ACCESS)` mở tiến trình trực
  tiếp bằng PID, bỏ qua bước tìm theo tên.
- `attach(HANDLE handle)` nhận một `HANDLE` bạn đã tự mở từ trước.
  `Process` sẽ sở hữu handle này và tự đóng nó khi gọi `detach()` hoặc khi
  đối tượng `Process` bị hủy.

Cả ba đều trả về `bool`. `false` nghĩa là không tìm thấy tiến trình, hoặc
`OpenProcess` thất bại (thường do không đủ quyền); kiểm tra `lastError()`
để biết mã lỗi Win32 cụ thể.

`detach()` đóng handle và đưa đối tượng về trạng thái chưa attach. Gọi
nhiều lần cũng không sao, và nó tự động chạy trong destructor, nên một
`Process` ra khỏi scope luôn tự dọn dẹp. `isAttached()` cho biết hiện có
đang giữ handle hay không. `pid()`, `handle()`, `processName()` trả về PID,
handle thô, và tên file thực thi đã attach.

## Đọc bộ nhớ

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

`read<T>(address)` copy `sizeof(T)` byte từ tiến trình đích và trả về dưới
dạng `T`. `T` bắt buộc phải là kiểu trivially copyable, vì bản chất thao
tác này là copy byte thô; trình biên dịch sẽ báo lỗi ngay ở bước biên dịch
qua `static_assert` nếu bạn dùng sai kiểu. Khi đọc thất bại, hàm trả về
`T{}` mặc định, tiện nhưng không phân biệt được với một giá trị 0 thật sự.
Khi sự khác biệt đó quan trọng, dùng `tryRead<T>(address)` thay thế, hàm
này trả về `std::optional<T>` và rỗng khi đọc thất bại.

`readArray<T>(address, count)` đọc `count` phần tử liên tiếp kiểu `T`, trả
về dưới dạng `std::vector<T>`. Nếu đọc thất bại, vector sẽ rỗng, nên kiểm
tra `.empty()` trước khi truy cập phần tử.

`readBytes(address, size)` đọc một khối bộ nhớ thô vào `std::vector<uint8_t>`,
hữu ích cho những cấu trúc bạn không muốn định nghĩa thành kiểu C++, hoặc
làm dữ liệu đầu vào cho `scan()`.

`readString`/`readWString` đọc một chuỗi kết thúc bằng ký tự null, dạng
ANSI hoặc UTF-16, tối đa `maxLength` ký tự, trả về `std::nullopt` nếu đọc
thất bại. Cả hai dừng lại ở ký tự null đầu tiên tìm thấy trong khoảng đó;
nếu chuỗi thật dài hơn `maxLength`, nó sẽ bị cắt bớt.

## Ghi bộ nhớ

```cpp
game.write(addr, 9999);
game.write(addr, 3.14f);
game.write(addr, Vec3{1.0f, 2.0f, 3.0f});

game.writeArray<int>(addr, waveIds);
game.writeBytes(addr, raw);
game.writeString(addr, "hello");
```

`write<T>(address, value)` và `writeArray<T>(address, span)` là phiên bản
ngược của các hàm đọc tương ứng, trả về `bool`. `writeString` ghi các byte
của chuỗi kèm theo một ký tự null ở cuối, nên hãy chắc chắn vùng nhớ đích
trong tiến trình đủ lớn để chứa nó.

## Pointer Chain (chuỗi con trỏ)

Game và hầu hết ứng dụng không đơn giản đều lưu dữ liệu qua nhiều lớp con
trỏ: một offset tĩnh trong module trỏ tới một địa chỉ khác, địa chỉ đó lại
chứa một con trỏ khác nữa, cứ thế cho tới khi bước cuối cùng mới chạm được
giá trị thật. `pointer()` giúp bạn đi qua chuỗi đó mà không cần viết vòng
lặp tay.

```cpp
uintptr_t base = game.module(L"client.dll").base();

int hp = game.pointer(base)
             .offset(0x10)
             .offset(0x20)
             .offset(0x8)
             .read<int>();
```

Mỗi lần gọi `.offset(x)`, thư viện sẽ dereference địa chỉ *hiện tại* (đọc
giá trị con trỏ nằm ở đó), rồi cộng thêm `x` vào kết quả. Điều đó có nghĩa
là giá trị bạn dùng để bắt đầu chuỗi phải tự nó là một vị trí chứa con trỏ,
thường là module base cộng với một offset tĩnh, chứ không phải địa chỉ dữ
liệu cuối cùng. Nếu bất kỳ bước dereference nào trong chuỗi trả về 0 (con
trỏ null, hoặc đọc thất bại), chuỗi sẽ chuyển sang trạng thái không hợp lệ
và mọi lệnh gọi tiếp theo chỉ là no-op rẻ tiền, trả về `T{}` hoặc
`std::nullopt` thay vì làm crash chương trình.

`.address()` cho bạn địa chỉ thô đã giải được, dùng khi bạn cần thứ gì đó
khác ngoài việc đọc giá trị có kiểu. `.valid()` / `explicit operator bool()`
cho biết chuỗi có giải thành công hay không. `.write<T>(value)` ghi qua con
trỏ đã giải, tương tự cách `.read<T>()` đọc qua nó.

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

`module(name)` tìm một module đã load theo tên (không phân biệt hoa
thường) và trả về `Module`. Nếu không tìm thấy, bạn nhận lại một `Module`
mặc định, không hợp lệ, chứ không phải exception hay con trỏ null; hãy
kiểm tra `.valid()`, hoặc dùng trực tiếp đối tượng trong ngữ cảnh boolean
(`if (client)`) trước khi tin tưởng `base()`. `modules()` trả về toàn bộ
module đang load trong tiến trình đã attach.

## Pattern Scan (quét pattern)

Địa chỉ hardcode sẽ hỏng ngay khi phần mềm đích được cập nhật phiên bản
mới. Pattern scan tìm một chuỗi byte, có thể chứa byte wildcard, bên trong
bộ nhớ tiến trình, giúp bạn định vị code hoặc dữ liệu theo "chữ ký" của nó
thay vì một địa chỉ cố định.

```cpp
auto addr = game.scan(L"client.dll", "48 8B 05 ?? ?? ?? ?? 48 85 C0");
if (addr) {
    uintptr_t functionStart = *addr;
}

auto addr2 = game.scan(base, moduleSize, "8B 44 24 08 ?? 90");
```

Định dạng pattern là các byte hex cách nhau bằng dấu cách, với `?` hoặc
`??` đại diện cho byte có thể là bất kỳ giá trị nào (cùng định dạng
AOB/IDA-style mà Cheat Engine và hầu hết disassembler dùng). `scan(moduleName,
pattern)` quét toàn bộ một module; `scan(start, size, pattern)` quét một
khoảng địa chỉ bất kỳ, đây cũng chính là hàm mà overload theo module gọi
bên trong sau khi đã lấy được base và size của module đó. Cả hai đều trả về
`std::optional<uintptr_t>`, rỗng nếu không tìm thấy gì khớp.

Bên trong, hàm scan duyệt qua bộ nhớ tiến trình đích theo từng vùng bằng
`VirtualQueryEx`, bỏ qua những vùng chưa commit hoặc bị đánh dấu
`PAGE_NOACCESS`/`PAGE_GUARD`, nên nó sẽ không thất bại hoàn toàn chỉ vì một
phần địa chỉ của module đang không map được vào bộ nhớ.

## Cấp phát bộ nhớ

```cpp
uintptr_t buffer = game.allocate(256);
uintptr_t buffer2 = game.allocate(256, Protection::ReadWrite);
uintptr_t buffer3 = game.allocate(256, PAGE_READWRITE);

game.free(buffer);
```

`allocate(size, protect)` bọc `VirtualAllocEx` với cờ `MEM_COMMIT |
MEM_RESERVE`, trả về địa chỉ vùng nhớ mới, hoặc 0 nếu thất bại. Quyền mặc
định là `PAGE_EXECUTE_READWRITE`, giữ đúng hành vi của thư viện gốc; hãy
truyền một quyền hẹp hơn nếu bạn không cần vùng nhớ có thể thực thi.
`free(address)` giải phóng vùng nhớ trước đó được `allocate()` cấp.

## Đổi quyền bảo vệ trang nhớ

```cpp
DWORD oldProtect = 0;
game.protect(addr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect);
game.write(addr, 9999);
game.restore(addr, sizeof(int), oldProtect);
```

`protect(address, size, newProtect, oldProtect = nullptr)` bọc
`VirtualProtectEx`. Nếu `oldProtect` khác null, giá trị quyền cũ sẽ được
ghi vào đó để bạn có thể khôi phục lại sau. `restore(address, size,
oldProtect)` chính là lệnh "khôi phục lại" đó, về bản chất nó gọi
`protect()` nhưng đặt tên rõ ràng để ai đọc code cũng hiểu ngay mục đích.
Cũng có overload `protect()` nhận `Protection` thay vì `DWORD`, nếu bạn
muốn viết `Protection::ReadWrite` thay vì phải nhớ đúng hằng số `PAGE_*`.

Thư viện không tự động khôi phục quyền bảo vệ. Nếu bạn đổi quyền, hãy tự
gọi `restore()` đi kèm, tốt nhất là ngay sau thao tác ghi cần đến quyền
mới đó.

## API Reference

### `mem::Process`

| Hàm | Tham số | Trả về | Ghi chú |
|---|---|---|---|
| `attach(name, access = PROCESS_ALL_ACCESS)` | `name`: tên file thực thi cần tìm. `access`: quyền truy cập mong muốn khi mở process. | `bool` | Tìm tiến trình theo tên rồi mở nó. |
| `attach(pid, access = PROCESS_ALL_ACCESS)` | `pid`: process ID. `access`: quyền truy cập. | `bool` | Mở tiến trình trực tiếp, không cần tìm theo tên. |
| `attach(handle)` | `handle`: một `HANDLE` đã mở sẵn. | `bool` | Nhận sở hữu handle; `Process` sẽ tự đóng nó sau này. |
| `detach()` | không | `void` | Đóng handle. Gọi nhiều lần vẫn an toàn. |
| `isAttached()` | không | `bool` | Hiện có đang giữ handle hay không. |
| `pid()` | không | `DWORD` | PID tiến trình đã attach, 0 nếu chưa attach. |
| `handle()` | không | `HANDLE` | Handle thô, dùng khi cần gọi trực tiếp API Win32 mà thư viện chưa bọc. |
| `processName()` | không | `const std::wstring&` | Tên file thực thi được xác định lúc attach. |
| `module(name)` | `name`: tên file module. | `Module` | Không hợp lệ (`.valid() == false`) nếu không tìm thấy. |
| `modules()` | không | `std::vector<Module>` | Toàn bộ module đã load trong tiến trình. |
| `static processes()` | không | `std::vector<ProcessEntry>` | Mọi tiến trình đang chạy; không cần `attach()` trước. |
| `read<T>(address)` | `address`: địa chỉ nguồn. | `T` | `T{}` nếu thất bại. `T` phải trivially copyable. |
| `tryRead<T>(address)` | `address`: địa chỉ nguồn. | `std::optional<T>` | `std::nullopt` nếu thất bại. |
| `write<T>(address, value)` | `address`: địa chỉ đích. `value`: dữ liệu cần ghi. | `bool` | |
| `readArray<T>(address, count)` | `address`: nguồn. `count`: số phần tử. | `std::vector<T>` | Vector rỗng nếu thất bại. |
| `writeArray<T>(address, values)` | `address`: đích. `values`: `std::span<const T>`. | `bool` | |
| `readBytes(address, size)` | `address`, `size` tính bằng byte. | `std::vector<uint8_t>` | Rỗng nếu thất bại. |
| `writeBytes(address, bytes)` | `address`. `bytes`: `std::span<const uint8_t>`. | `bool` | |
| `readString(address, maxLength = 256)` | `address`. `maxLength`: giới hạn ký tự. | `std::optional<std::string>` | Chuỗi ANSI, dừng ở byte null đầu tiên. |
| `readWString(address, maxLength = 256)` | tương tự | `std::optional<std::wstring>` | Phiên bản UTF-16. |
| `writeString(address, text)` | `address`. `text`: `std::string_view`. | `bool` | Ghi kèm ký tự null ở cuối. |
| `dereference(address)` | `address` | `uintptr_t` | Đọc một `uintptr_t` tại `address`; được `Pointer::offset` dùng bên trong. |
| `pointer(base)` | `base`: địa chỉ bắt đầu. | `Pointer` | Bắt đầu một chuỗi pointer; xem phần Pointer Chain. |
| `scan(moduleName, pattern)` | `moduleName`. `pattern`: chuỗi AOB. | `std::optional<uintptr_t>` | Quét trong một module. |
| `scan(start, size, pattern)` | `start`, `size`, `pattern`. | `std::optional<uintptr_t>` | Quét một khoảng địa chỉ bất kỳ. |
| `protect(address, size, newProtect, oldProtect = nullptr)` | `newProtect`: `DWORD` thô hoặc `Protection`. `oldProtect`: tham số ra tùy chọn. | `bool` | Bọc `VirtualProtectEx`. |
| `restore(address, size, oldProtect)` | `oldProtect`: giá trị lấy từ `protect()`. | `bool` | Khôi phục lại quyền cũ. |
| `allocate(size, protect = PAGE_EXECUTE_READWRITE)` | `size` byte. `protect`: `DWORD` thô hoặc `Protection`. | `uintptr_t` | 0 nếu thất bại. |
| `free(address)` | `address` lấy từ `allocate()`. | `bool` | Bọc `VirtualFreeEx`. |
| `lastError()` | không | `DWORD` | Mã `GetLastError()` của lệnh gọi thất bại gần nhất. |

### `mem::Pointer`

Được tạo ra từ `Process::pointer(base)`, không tự khởi tạo trực tiếp.

| Hàm | Trả về | Ghi chú |
|---|---|---|
| `offset(value)` | `Pointer&` | Dereference địa chỉ hiện tại, cộng thêm `value`. Có thể nối chuỗi. |
| `address()` | `uintptr_t` | Địa chỉ đã giải được tính tới bước hiện tại. |
| `valid()` / `operator bool()` | `bool` | `false` khi có bước dereference nào trong chuỗi trả về 0. |
| `read<T>()` | `T` | Đọc tại địa chỉ đã giải; `T{}` nếu chuỗi không hợp lệ. |
| `tryRead<T>()` | `std::optional<T>` | Tương tự, báo thất bại qua `std::nullopt`. |
| `write<T>(value)` | `bool` | Ghi tại địa chỉ đã giải. |

### `mem::Module`

| Hàm | Trả về |
|---|---|
| `name()` | `const std::wstring&` |
| `path()` | `const std::wstring&` |
| `base()` | `uintptr_t` |
| `size()` | `size_t` |
| `valid()` / `operator bool()` | `bool` |

### `mem::Protection`

`enum class Protection : DWORD` với các giá trị `NoAccess`, `ReadOnly`,
`ReadWrite`, `Execute`, `ExecuteRead`, `ExecuteReadWrite`, là bí danh có
kiểu cho các hằng số `PAGE_*` tương ứng. `toWin32(Protection)` chuyển
ngược lại thành `DWORD` thô, dùng khi bạn cần truyền vào một API Win32 mà
thư viện chưa bọc.

### `mem::ProcessEntry`

Một struct đơn giản: `DWORD pid` và `std::wstring name`, mỗi entry đại
diện một tiến trình đang chạy, được trả về bởi `Process::processes()`.

## Ví dụ

**Đọc int, float, và struct**
```cpp
int ammo = game.read<int>(addr);
float speed = game.read<float>(addr + 0x4);

struct Vec3 { float x, y, z; };
Vec3 pos = game.read<Vec3>(addr + 0x8);
```

**Đọc chuỗi một cách an toàn**
```cpp
if (auto name = game.readString(addr, 32)) {
    std::cout << *name << "\n";
} else {
    std::cout << "doc that bai, ma loi: " << game.lastError() << "\n";
}
```

**Giải pointer chain rồi đọc qua nó**
```cpp
uintptr_t base = game.module(L"client.dll").base();
int hp = game.pointer(base).offset(0x10).offset(0x20).offset(0x8).read<int>();
```

**Tìm một hàm bằng pattern**
```cpp
if (auto addr = game.scan(L"client.dll", "55 8B EC 83 EC ?? 53 56 57")) {
    uintptr_t functionAddress = *addr;
}
```

**Cấp phát buffer, ghi dữ liệu, rồi giải phóng**
```cpp
uintptr_t buffer = game.allocate(64, Protection::ReadWrite);
if (buffer) {
    game.writeBytes(buffer, someBytes);
    game.free(buffer);
}
```

**Đổi quyền, ghi, khôi phục**
```cpp
DWORD oldProtect = 0;
if (game.protect(addr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect)) {
    game.write(addr, 1234);
    game.restore(addr, sizeof(int), oldProtect);
}
```

**Một chương trình hoàn chỉnh**
```cpp
#include "mem.hpp"
#include <iostream>

int main() {
    mem::Process game;
    if (!game.attach(L"PlantsVsZombies.exe")) {
        std::cerr << "attach that bai, ma loi " << game.lastError() << "\n";
        return 1;
    }

    int sun = game.read<int>(0x1B0F6740);
    std::cout << "sun: " << sun << "\n";
    game.write(0x1B0F6740, 9999);

    game.detach();
}
```

## Nên làm

- Ưu tiên `tryRead<T>` thay vì `read<T>` khi giá trị 0 có thể là một kết
  quả đọc hợp lệ và bạn cần phân biệt nó với một lần đọc thất bại.
- Kiểm tra `module()`/`Pointer::valid()` trước khi tin tưởng `base()` hay
  một địa chỉ đã giải; cả hai đều thất bại âm thầm theo thiết kế, không
  ném exception.
- Chỉ xin quyền `OpenProcess` vừa đủ cho việc bạn thực sự làm.
  `PROCESS_ALL_ACCESS` là mặc định để giữ đúng hành vi thư viện gốc, nhưng
  `PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
  PROCESS_QUERY_INFORMATION` thường là đủ, và dễ thành công hơn khi không
  chạy với quyền administrator.
- Luôn đi kèm `protect()` với `restore()`. Thư viện không tự làm việc đó
  giúp bạn, và để một trang nhớ ở quyền lỏng hơn mức cần thiết là kiểu lỗi
  rất dễ quên và khó debug về sau.
- Build project cùng kiến trúc (x86 hoặc x64) với tiến trình đích.
- Chạy lại `scan()` sau khi phần mềm đích cập nhật. Đó chính là lý do dùng
  pattern thay vì địa chỉ cố định, nhưng bản thân pattern cũng có thể lỗi
  thời nếu đoạn code xung quanh thay đổi hoàn toàn.

## Lỗi thường gặp

- Coi `read<T>()` trả về `0` là bằng chứng đọc thất bại. Có thể đúng, cũng
  có thể giá trị thật sự là 0. Dùng `tryRead<T>()` nếu điều này quan
  trọng.
- Quên rằng `pointer(base)` yêu cầu `base` phải trỏ *tới* một con trỏ, chứ
  không phải trỏ thẳng tới giá trị cuối cùng. Nếu offset đầu tiên cho ra
  kết quả sai, khả năng cao bạn đang thiếu hoặc thừa một bước dereference.
- Viết pattern không có dấu cách (`"488B05????"` thay vì
  `"48 8B 05 ?? ?? ?? ??"`). Bộ phân tích pattern tách chuỗi theo dấu
  cách, nên một pattern không có dấu cách sẽ bị đọc thành một token duy
  nhất, không hợp lệ.
- Gọi `write()`/`allocate()` với `PAGE_EXECUTE_READWRITE` theo thói quen
  dù vùng nhớ không cần thực thi. Vẫn chạy được, nhưng đó là quyền rộng
  hơn mức hầu hết thao tác ghi cần tới.
- Nhầm lẫn giữa tiến trình 32-bit và build 64-bit (hoặc ngược lại). Con
  trỏ đọc ra sẽ bị cắt cụt hoặc sai lệch, và nó không nhất thiết báo lỗi
  rõ ràng ngay lập tức.

# Lời cảm ơn

Thư viện này được phát triển dựa trên những kiến thức và ý tưởng học được
từ các video hướng dẫn của kênh YouTube Cheat and Math.

Xin chân thành cảm ơn Cheat and Math đã chia sẻ rất nhiều kiến thức hữu
ích về lập trình và thao tác bộ nhớ trên Windows.

YouTube:
https://www.youtube.com/@cheat_and_math

Thư viện này là một phiên bản được thiết kế lại và mở rộng, không phải là
bản sao của source gốc.
