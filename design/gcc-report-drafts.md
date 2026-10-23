# GCC report drafts (for Alex to file himself)

Drafts only. Nothing has been sent. Alex chose on 2026-10-23 that these be drafted and that he
files them. Before filing, search GCC Bugzilla by hand for duplicates: automated queries to
gcc.gnu.org/bugzilla are refused by its bot protection (2026-10-23), and a web search found no
match for F1. Also check whether a GCC 16 snapshot newer than 16.2.1 20260810 still shows each.

## F1. Wrong index from std::string_view::find with a start offset in a constexpr function

**Component:** c++ (or libstdc++; the narrowing below points at constant folding in the front end).
**Version:** g++ (GCC) 16.2.1 20260810, Arch Linux package `gcc 16.2.1+r23+gd564253eb6c8-1`, x86_64.
**Options:** `-std=c++26 -O1` (also `-O2`). Correct at `-O0` in the reduced test and with `-std=c++23 -O0`.

Testcase:

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

Observed at `-O1` and `-O2`: "f literal=3" and "g literal=3"; "f var=2" and "f constexpr=2".
Expected: 2 for all four. clang 22.1.8 prints 2 for all four with libstdc++ 16 and with libc++.

Narrowing: plain C `memchr` from an offset, `__builtin_memchr` in C++, `std::char_traits<char>::find`,
and a non-constexpr function calling `string_view::find` all give the right index at `-O0`, `-O1`
and `-O2`. The defect needs a constexpr function called with a string literal outside a manifestly
constant-evaluated context. The result is the right index plus the start offset, which suggests
that the folding of `memchr` (behind `find`) adds the offset twice. In a larger program the same
defect appeared without optimization (a constexpr `split` of "/pkg.Service/Method" returned a
service name one character too long at `-O0`).

## F2. -fsanitize=null rejects a valid constant expression (std::string from a pointer into an array)

Likely the same root cause as PR 71962 (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71962):
`-fsanitize=null` implies `-fno-delete-null-pointer-checks`, which stops constant folding of an
address compared with null. If PR 71962 is open, add this as a comment there rather than a new
report.

**Version:** as for F1. **Options:** `-std=c++26 -fsanitize=null` (also `nonnull-attribute`,
`returns-nonnull-attribute`, or `-fno-delete-null-pointer-checks` alone).

Testcase:

```cpp
#include <string>
struct S { static constexpr char data[] = "abcdef"; };
consteval std::size_t f() { std::string s(S::data + 2, 3); return s.size(); }
static_assert(f() == 3);
int main() {}
```

Observed: `error: non-constant condition for static assertion` and
`basic_string.h:713:17: error: '(((const char*)(&*(const char (*)[7])(&S::data[2]))) == 0)' is not a
constant expression`. Expected: accepted, as without the option. Effect: UBSan cannot be used on
code that builds `std::string` from such pointers in constant evaluation, for example C++26
reflection code that builds names at compile time.
