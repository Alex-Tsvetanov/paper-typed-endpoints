// Expected to fail: a name defined twice in one scope, reported with its own name and location.
#include "tep/reflect.hpp"
struct dup_proto {
    static constexpr std::string_view text = "syntax = \"proto3\";\nmessage DuplicatedMessage {}\nmessage DuplicatedMessage {}\n";
};
consteval { tep::define_messages<dup_proto>(); }
int main() {}
