// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The portable proto3 front end: valid schemas (at compile time and at run time) and one
// malformed schema per diagnostic, each with its line and column.

#include <optional>
#include <string>
#include <string_view>

#include "check.hpp"
#include "tep/schema.hpp"

using namespace tep::schema;

namespace {

constexpr std::string_view greeter = R"(syntax = "proto3";
package tep.demo;

// A line comment.
/* A block
   comment. */
option java_package = "demo";
option optimize_for = SPEED;

message HelloRequest {
  string name = 1 [json_name = "n", deprecated = true];
  int32 times = 0x2;
}

message HelloReply {
  string message = 1;
  repeated int32 echoes = 2;
}

service Greeter {
  option deprecated = false;
  rpc SayHello(HelloRequest) returns (HelloReply);
  rpc SayNothing(.tep.demo.HelloRequest) returns (HelloReply) {
    option idempotency_level = NO_SIDE_EFFECTS;
  }
}
)";

constexpr bool greeter_parses() {
    const auto f = parse(greeter);
    if (!f) return false;
    if (f->package != "tep.demo" || f->messages.size() != 2 || f->services.size() != 1) return false;
    const service& s = f->services[0];
    if (s.full_name != "tep.demo.Greeter" || s.rpcs.size() != 2) return false;
    if (s.rpcs[0].request != 0 || s.rpcs[0].response != 1 || s.rpcs[1].request != 0) return false;
    const message& q = f->messages[0];
    return q.full_name == "tep.demo.HelloRequest" && q.fields.size() == 2 && q.fields[1].number == 2 &&
           q.fields[0].type == scalar::string && f->messages[1].fields[1].repeated && f->messages[1].fields[1].packed;
}
static_assert(greeter_parses(), "the greeter schema parses in constant evaluation");

constexpr std::optional<error> first_error(std::string_view src) {
    const auto f = parse(src);
    if (f) return std::nullopt;
    return f.error();
}

constexpr bool fails_with(std::string_view src, errc code, std::uint32_t line, std::uint32_t column) {
    const auto e = first_error(src);
    return e && e->code == code && e->loc.line == line && e->loc.column == column;
}

static_assert(fails_with("syntax = \"proto3\";\nmessage A {\n  int32 a = 1\n  int32 b = 2;\n}\n",
                         errc::unexpected_token, 4, 3),
              "a missing semicolon is a compile-time error with its location");

struct bad_case {
    const char* what;
    const char* source;
    errc code;
    std::uint32_t line;
    std::uint32_t column;
};

#define P3 "syntax = \"proto3\";\n"

constexpr bad_case bad_cases[] = {
    {"missing semicolon", P3 "message A {\n  int32 a = 1\n  int32 b = 2;\n}\n", errc::unexpected_token, 4, 3},
    {"missing field number", P3 "message A { int32 a = ; }\n", errc::expected_integer, 2, 23},
    {"field number zero", P3 "message A { int32 a = 0; }\n", errc::field_number_out_of_range, 2, 23},
    {"field number above 2^29-1", P3 "message A { int32 a = 536870912; }\n", errc::field_number_out_of_range, 2, 23},
    {"implementation range", P3 "message A { int32 a = 19000; }\n", errc::field_number_in_implementation_range, 2, 23},
    {"duplicate field number", P3 "message A {\n  int32 a = 1;\n  int32 b = 1;\n}\n", errc::duplicate_field_number, 4, 13},
    {"duplicate field name", P3 "message A {\n  int32 a = 1;\n  string a = 2;\n}\n", errc::duplicate_field_name, 4, 10},
    {"reserved number", P3 "message A {\n  reserved 2, 5 to 7;\n  int32 a = 6;\n}\n", errc::field_number_reserved, 4, 13},
    {"reserved name", P3 "message A {\n  reserved \"a\";\n  int32 a = 1;\n}\n", errc::field_name_reserved, 4, 9},
    {"bad reserved range", P3 "message A { reserved 9 to 3; }\n", errc::bad_reserved_range, 2, 22},
    {"unknown type", P3 "message A { Missing m = 1; }\n", errc::unknown_type, 2, 13},
    {"map field", P3 "message A { map<string, int32> m = 1; }\n", errc::unsupported_map, 2, 13},
    {"oneof", P3 "message A { oneof x { int32 a = 1; } }\n", errc::unsupported_oneof, 2, 13},
    {"proto3 optional", P3 "message A { optional int32 a = 1; }\n", errc::unsupported_optional, 2, 13},
    {"required label", P3 "message A { required int32 a = 1; }\n", errc::unsupported_label, 2, 13},
    {"group", P3 "message A { repeated group G = 1 {} }\n", errc::unsupported_group, 2, 22},
    {"extensions", P3 "message A { extensions 100 to 199; }\n", errc::unsupported_extensions, 2, 13},
    {"import", P3 "import \"other.proto\";\n", errc::unsupported_import, 2, 1},
    {"streaming request", P3 "message A {}\nservice S { rpc M(stream A) returns (A); }\n", errc::unsupported_streaming, 3, 19},
    {"streaming response", P3 "message A {}\nservice S { rpc M(A) returns (stream A); }\n", errc::unsupported_streaming, 3, 31},
    {"rpc on an enum", P3 "enum E { Z = 0; }\nmessage A {}\nservice S { rpc M(E) returns (A); }\n", errc::not_a_message_type, 4, 19},
    {"rpc on an unknown type", P3 "message A {}\nservice S { rpc M(B) returns (A); }\n", errc::unknown_type, 3, 19},
    {"proto2", "syntax = \"proto2\";\n", errc::syntax_not_proto3, 1, 10},
    {"missing syntax", "message A {}\n", errc::missing_syntax, 1, 1},
    {"editions", "edition = \"2023\";\n", errc::unsupported_editions, 1, 1},
    {"enum first value nonzero", P3 "enum E { A = 1; }\n", errc::enum_first_value_not_zero, 2, 10},
    {"empty enum", P3 "enum E { }\n", errc::empty_enum, 2, 6},
    {"enum alias", P3 "enum E { A = 0; B = 0; }\n", errc::enum_value_alias, 2, 17},
    {"duplicate enum value", P3 "enum E { A = 0; A = 1; }\n", errc::duplicate_enum_value_name, 2, 17},
    {"enum value out of range", P3 "enum E { A = 0; B = 2147483648; }\n", errc::enum_value_out_of_range, 2, 21},
    {"unterminated message", P3 "message A {\n  int32 a = 1;\n", errc::unexpected_end, 4, 1},
    {"recursive message", P3 "message A { B b = 1; }\nmessage B { A a = 1; }\n", errc::recursive_message, 3, 13},
    {"self-recursive repeated", P3 "message A { repeated A kids = 1; }\n", errc::recursive_message, 2, 22},
    {"packed on a string", P3 "message A { repeated string s = 1 [packed = true]; }\n", errc::packed_on_non_packable, 2, 29},
    {"packed on a singular", P3 "message A { int32 s = 1 [packed = true]; }\n", errc::packed_on_non_packable, 2, 19},
    {"proto2 default", P3 "message A { int32 a = 1 [default = 3]; }\n", errc::unsupported_option, 2, 26},
    {"message_set_wire_format", P3 "message A { option message_set_wire_format = true; }\n", errc::unsupported_option, 2, 20},
    {"duplicate definition", P3 "message A {}\nmessage A {}\n", errc::duplicate_definition, 3, 9},
    {"duplicate package", P3 "package a;\npackage b;\n", errc::duplicate_package, 3, 1},
    {"invalid character", P3 "message A { int32 a = 1; @ }\n", errc::invalid_character, 2, 26},
    {"unterminated comment", P3 "/* never closed\n", errc::unterminated_comment, 2, 1},
    {"unterminated string", "syntax = \"proto3;\n", errc::unterminated_string, 1, 10},
    {"integer overflow", P3 "message A { int32 a = 99999999999999999999; }\n", errc::integer_overflow, 2, 23},
    {"stray top-level token", P3 "message A {}\nfoo\n", errc::unexpected_token, 3, 1},
    // protoc resolves the first component of a dotted name innermost first, then requires the rest.
    {"dotted name, first component", P3 "package p;\nmessage A { message B {} }\nmessage M { message A {} A.B f = 1; }\n",
     errc::unknown_type, 4, 26},
    // C++-like scoping: enum values live in the scope that holds their enum.
    {"enum values share a scope", P3 "enum A { UNKNOWN = 0; }\nenum B { UNKNOWN = 0; }\n", errc::duplicate_definition, 3, 10},
    {"message and enum value", P3 "message X {}\nenum A { X = 0; }\n", errc::duplicate_definition, 3, 10},
    {"field and nested message", P3 "message M { message foo {} int32 foo = 1; }\n", errc::duplicate_definition, 2, 34},
    {"duplicate rpc", P3 "message A {}\nservice S { rpc M(A) returns (A); rpc M(A) returns (A); }\n",
     errc::duplicate_definition, 3, 39},
    {"json name conflict", P3 "message M { int32 foo_bar = 1; int32 fooBar = 2; }\n", errc::json_name_conflict, 2, 38},
    {"enum prefix conflict", P3 "enum Color { COLOR_RED = 0; RED = 1; }\n", errc::enum_value_name_conflict, 2, 29},
    {"reserved zero", P3 "message A { reserved 0; }\n", errc::field_number_out_of_range, 2, 22},
    {"reserved overlap", P3 "message A { reserved 1 to 5, 4; }\n", errc::reserved_overlap, 2, 30},
    {"reserved name twice", P3 "message A { reserved \"x\", \"x\"; }\n", errc::duplicate_reserved_name, 2, 27},
    {"allow_alias unused", P3 "enum E { option allow_alias = true; A = 0; }\n", errc::allow_alias_unused, 2, 6},
    {"packed false on a singular", P3 "message A { int32 s = 1 [packed = false]; }\n", errc::packed_on_non_packable, 2, 19},
    {"packed twice", P3 "message A { repeated int32 s = 1 [packed = true, packed = false]; }\n",
     errc::unsupported_option, 2, 50},
    {"editions feature option", P3 "option features.field_presence = EXPLICIT;\n", errc::unsupported_editions, 2, 8},
    {"unknown file option", P3 "option no_such_option = 1;\n", errc::unsupported_option, 2, 8},
    {"unknown field option", P3 "message A { int32 a = 1 [no_such = 1]; }\n", errc::unsupported_option, 2, 26},
    {"custom option", P3 "message A {}\nservice S { rpc M(A) returns (A) { option (google.api.http) = { get: \"/v\" }; } }\n",
     errc::unsupported_option, 3, 43},
};

#undef P3

std::string nested(int depth) {
    std::string s = "syntax = \"proto3\";\n";
    for (int i = 0; i < depth; ++i) s += "message M" + std::to_string(i) + " { ";
    for (int i = 0; i < depth; ++i) s += "} ";
    return s;
}

void test_bad_cases() {
    for (const bad_case& c : bad_cases) {
        const auto e = first_error(c.source);
        const bool ok = e && e->code == c.code && e->loc.line == c.line && e->loc.column == c.column;
        if (!ok) {
            std::fprintf(stderr, "case '%s': ", c.what);
            if (e) {
                std::fprintf(stderr, "got '%.*s' at %u:%u\n", static_cast<int>(describe(e->code).size()),
                             describe(e->code).data(), e->loc.line, e->loc.column);
            } else {
                std::fprintf(stderr, "parsed without error\n");
            }
        }
        TEP_CHECK(ok);
    }
    TEP_CHECK(parse(nested(static_cast<int>(max_nesting))).has_value());
    const auto deep = first_error(nested(static_cast<int>(max_nesting) + 1));
    TEP_CHECK(deep && deep->code == errc::nesting_too_deep);
}

void test_resolution() {
    constexpr std::string_view src = R"(syntax = "proto3";
package a.b;
message Outer {
  message Inner { int32 x = 1; }
  enum Kind { K0 = 0; K1 = 1; }
  Inner inner = 1;
  Kind kind = 2;
  repeated Outer.Inner more = 3;
}
message Uses {
  Outer.Inner a = 1;
  .a.b.Outer.Kind k = 2;
  Inner shadow = 3;
}
message Inner { string y = 1; }
)";
    const auto f = parse(src);
    TEP_CHECK(f.has_value());
    if (!f) return;
    const std::uint32_t outer = f->find_message("a.b.Outer");
    const std::uint32_t inner = f->find_message("a.b.Outer.Inner");
    const std::uint32_t uses = f->find_message("a.b.Uses");
    const std::uint32_t top_inner = f->find_message("a.b.Inner");
    const std::uint32_t kind = f->find_enum("a.b.Outer.Kind");
    TEP_CHECK(outer != npos && inner != npos && uses != npos && top_inner != npos && kind != npos);
    const message& o = f->messages[outer];
    TEP_CHECK(o.fields[0].kind == field_kind::message && o.fields[0].type_index == inner);
    TEP_CHECK(o.fields[1].kind == field_kind::enumeration && o.fields[1].type_index == kind);
    TEP_CHECK(o.fields[2].type_index == inner && o.fields[2].repeated && !o.fields[2].packed);
    const message& u = f->messages[uses];
    TEP_CHECK(u.fields[0].type_index == inner);
    TEP_CHECK(u.fields[1].kind == field_kind::enumeration && u.fields[1].type_index == kind);
    TEP_CHECK(u.fields[2].type_index == top_inner);  // innermost scope first: a.b.Uses.Inner, then a.b.Inner
    // Dependencies come first in definition order.
    std::size_t pos_inner = 0, pos_outer = 0, pos_uses = 0, pos_top = 0;
    for (std::size_t i = 0; i < f->definition_order.size(); ++i) {
        if (f->definition_order[i] == inner) pos_inner = i;
        if (f->definition_order[i] == outer) pos_outer = i;
        if (f->definition_order[i] == uses) pos_uses = i;
        if (f->definition_order[i] == top_inner) pos_top = i;
    }
    TEP_CHECK(f->definition_order.size() == f->messages.size());
    TEP_CHECK(pos_inner < pos_outer && pos_outer < pos_uses && pos_top < pos_uses);
}

void test_details() {
    // A service before the messages it uses is fine (order of definitions does not matter).
    const auto f = parse(R"(syntax = "proto3";
service S { rpc M(A) returns (A); }
message A { int32 x = 010; repeated bool flags = 2 [packed = false]; repeated sint64 v = 3; }
enum E { option allow_alias = true; Z = 0; ZERO = 0; NEG = -5; reserved 7, 9 to max; reserved "OLD"; }
)");
    TEP_CHECK(f.has_value());
    if (!f) return;
    TEP_CHECK(f->messages.size() == 1 && f->services.size() == 1 && f->services[0].rpcs[0].request == 0);
    TEP_CHECK(f->messages[0].fields[0].number == 8);  // octal literal
    TEP_CHECK(!f->messages[0].fields[1].packed);      // [packed = false]
    TEP_CHECK(f->messages[0].fields[2].packed);       // proto3 default for repeated numeric fields
    TEP_CHECK(f->enums[0].values[2].number == -5);
    // Keywords of other contexts are plain identifiers where a name is expected.
    TEP_CHECK(parse("syntax = \"proto3\"; message A { int32 message = 1; int32 service = 2; int32 map = 3; }").has_value());
    // The error token views the source: the type as written, and the duplicate's own name.
    const auto e = first_error("syntax = \"proto3\";\nmessage A { a.Missing m = 1; }\n");
    TEP_CHECK(e && e->token == "a.Missing");
    const auto d =
        first_error("syntax = \"proto3\";\nmessage LongEnoughNameToLeaveSso {}\nmessage LongEnoughNameToLeaveSso {}\n");
    TEP_CHECK(d && d->code == errc::duplicate_definition && d->token == "LongEnoughNameToLeaveSso");
    // Aliases may differ only by the prefix; values that do not collide are fine.
    TEP_CHECK(parse("syntax = \"proto3\"; enum Color { option allow_alias = true; COLOR_RED = 0; RED = 0; BLUE = 1; }")
                  .has_value());
    TEP_CHECK(parse("syntax = \"proto3\"; enum Foo { FOO_BAR_BAZ = 0; FOO_BARBAZ = 1; }").has_value());
    // A package component is a scope for dotted names.
    TEP_CHECK(parse("syntax = \"proto3\"; package a.b; message X {} message Y { b.X x = 1; a.b.X y = 2; }").has_value());
}

}  // namespace

int main() {
    TEP_CHECK(greeter_parses());
    test_bad_cases();
    test_resolution();
    test_details();
    return tep_test::finish("test_schema");
}
