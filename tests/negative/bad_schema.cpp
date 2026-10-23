// Expected to fail: a schema error is a compile error that names its line and column.
#include "tep/reflect.hpp"
struct bad_proto {
    static constexpr std::string_view text = "syntax = \"proto3\";\nmessage A {\n  int32 a = 1;\n  int32 b = 1;\n}\n";
};
consteval { tep::define_messages<bad_proto>(); }
int main() {}
