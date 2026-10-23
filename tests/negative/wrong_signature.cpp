// Expected to fail: a handler with the right name and the wrong signature.
#include "tep/reflect.hpp"
struct svc_proto {
    static constexpr std::string_view text =
        "syntax = \"proto3\";\npackage p;\nmessage A {}\nservice S { rpc Ping(A) returns (A); }\n";
};
consteval { tep::define_messages<svc_proto>(); }
using A = tep::message_t<svc_proto, "p.A">;
struct impl {
    int Ping(const A&) { return 0; }
};
int main() {
    impl h;
    tep::endpoint<svc_proto, "p.S", impl> ep{h};
    (void)ep;
}
