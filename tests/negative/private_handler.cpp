// Expected to fail: a handler member function must be public.
#include "tep/reflect.hpp"
struct svc_proto {
    static constexpr std::string_view text =
        "syntax = \"proto3\";\npackage p;\nmessage A {}\nservice S { rpc Ping(A) returns (A); rpc Pong(A) returns (A); }\n";
};
consteval { tep::define_messages<svc_proto>(); }
using A = tep::message_t<svc_proto, "p.A">;
class impl {
public:
    A Ping(const A& a) { return a; }

private:
    A Pong(const A& a) { return a; }
};
int main() {
    impl h;
    tep::endpoint<svc_proto, "p.S", impl> ep{h};
    (void)ep;
}
