# Compiler findings (development, functional only)

Found on the desktop on 2026-10-23 while building P5's skeleton. No timing is involved. Each entry
names the compiler, a minimal reproduction, and what this repository does about it. Reporting a
finding upstream is outbound and is Alex's decision.

## F1. GCC 16.2.1: `string_view::find` from an offset returns a wrong index

Compiler: GCC 16.2.1 20260810 (Arch package `gcc 16.2.1+r23+gd564253eb6c8-1`, image
`papers-dev:arch-2026-10-23`). Not reproduced with clang 22.1.8 (libstdc++ or libc++).

```cpp
#include <string_view>
#include <cstdio>
constexpr std::size_t f(std::string_view p) { std::string_view r = p.substr(1); return r.find('/'); }
constexpr std::size_t g(std::string_view p) { return p.find('/', 1) - 1; }
int main() {
  std::printf("f literal=%zu\n", f("/ab/c"));   // expected 2
  std::string_view v = "/ab/c";
  std::printf("f var=%zu\n", f(v));             // expected 2
  constexpr std::size_t c = f("/ab/c");
  std::printf("f constexpr=%zu\n", c);          // expected 2
  std::printf("g literal=%zu\n", g("/ab/c"));   // expected 2
}
```

Observed with `g++ -std=c++26`: at `-O0` all four print 2. At `-O1` and `-O2`, "f literal" and
"g literal" print 3, while "f var" and "f constexpr" print 2. With `-std=c++23 -O0` all print 2.
In the repository's own `split_path`, the defect showed even in a Debug build: the service name
came back one character too long.

Narrowing: plain C `memchr` from an offset, `__builtin_memchr` in C++, `char_traits<char>::find`,
and a non-constexpr function calling `string_view::find`, all print the right index at `-O0`,
`-O1` and `-O2`. The defect needs a constexpr function that receives a string literal. A likely
reading, not verified in GCC's sources: the front end folds the `memchr` behind `find` while it
tries to evaluate the constexpr call ahead of time, and adds the offset twice.

What this repository does: the portable layer uses no `string_view::find` in constexpr code
(`grpc.hpp` `split_path` and the lexer's symbol test use loops). `test_grpc` and `test_schema`
call these functions with literals, so a regression shows.

## F2. GCC 16.2.1: UBSan null checks break constant evaluation of the reflection layer

With `-fsanitize=null`, `-fsanitize=nonnull-attribute` or `-fsanitize=returns-nonnull-attribute`
(each alone), `tests/test_reflect.cpp` fails to compile with errors such as
`'(((const char*)(&*(const char (*)[1050])(&everything_proto::data[43]))) == 0)' is not a
constant expression`, raised in `basic_string` during the schema tables' constant evaluation.
The other UBSan checks and ASan compile it.

A reduced testcase (in `design/gcc-report-drafts.md`) fails the same way with
`-fno-delete-null-pointer-checks` alone, which matches GCC PR 71962 (`-fsanitize=null` implies that
option, and constant folding of an address compared with null stops).

What this repository does: the `linux-gcc-asan` preset uses
`-fsanitize=address,undefined -fno-sanitize=null,nonnull-attribute,returns-nonnull-attribute`.
This is a declared sanitizer gap for the reflection layer.

## F3. clang 22.1.8 with GCC 16's libstdc++: no `std::string` in constant evaluation

`std::string(std::string_view)` inside a constant evaluation fails with
"undefined function '_M_construct<const char *>' cannot be used in a constant expression".
With `-stdlib=libc++` the same code compiles. The clang presets use libc++.
