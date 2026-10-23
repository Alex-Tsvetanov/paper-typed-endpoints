// Expected to fail: streaming rpcs are refused at compile time (unary only).
#include "tep/reflect.hpp"
struct stream_proto {
    static constexpr std::string_view text =
        "syntax = \"proto3\";\nmessage A {}\nservice S { rpc M(stream A) returns (A); }\n";
};
consteval { tep::define_messages<stream_proto>(); }
int main() {}
