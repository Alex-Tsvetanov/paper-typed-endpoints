// Expected to fail: `class` (escaped to `class_` in C++) next to `class_`. Both have the JSON name
// "class", so the proto3 JSON-name rule rejects the pair before keyword escaping could collide.
#include "tep/reflect.hpp"
struct kw_proto {
    static constexpr std::string_view text = "syntax = \"proto3\";\nmessage A {\n  int32 class_ = 1;\n  int32 class = 2;\n}\n";
};
consteval { tep::define_messages<kw_proto>(); }
int main() {}
