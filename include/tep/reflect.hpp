// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// C++26 static reflection (P2996) layer: protobuf message types and unary gRPC endpoints are
// synthesized from .proto text at compile time, without protoc.
//
//   struct greeter_proto { static constexpr std::string_view text = R"(...)"; };
//   consteval { tep::define_messages<greeter_proto>(); }
//   using HelloRequest = tep::message_t<greeter_proto, "demo.HelloRequest">;
//   tep::endpoint<greeter_proto, "demo.Greeter", my_handler> ep{handler};
//
// The schema is parsed once per translation unit into static tables. A schema error is a
// compile error that names the line and column. There is no fallback path: without
// reflection this header does not compile.
#pragma once

#if !defined(__cpp_impl_reflection)
#error "tep/reflect.hpp needs C++26 static reflection (P2996), e.g. GCC 16 with -std=c++26 -freflection. There is no fallback path."
#endif

#include <meta>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tep/fixed_string.hpp"
#include "tep/grpc.hpp"
#include "tep/schema.hpp"
#include "tep/wire.hpp"

namespace tep {

// Thrown only during constant evaluation; the compiler prints what() in the diagnostic.
struct schema_error : std::exception {
    const char* text;
    constexpr explicit schema_error(const char* t) noexcept : text(t) {}
    [[nodiscard]] constexpr const char* what() const noexcept override { return text; }
};

template <class S>
concept schema_source = requires {
    { S::text } -> std::convertible_to<std::string_view>;
};

// A synthesized message: one aggregate per message of the schema, completed by
// define_messages<Schema>().
template <class Schema, std::size_t Index>
struct message;

template <class T>
struct message_traits;

template <class Schema, std::size_t Index>
struct message_traits<message<Schema, Index>> {
    using schema = Schema;
    static constexpr std::size_t index = Index;
};

template <class T>
concept synthesized_message = requires { message_traits<T>::index; };

// A singular message field: an optional value on the heap, so an absent sub-message costs one
// pointer instead of its whole size (a message type holds its sub-messages through boxes).
template <class T>
class box {
public:
    box() noexcept = default;
    box(const T& v) : p_(std::make_unique<T>(v)) {}
    box(T&& v) : p_(std::make_unique<T>(std::move(v))) {}
    box(const box& o) : p_(o.p_ ? std::make_unique<T>(*o.p_) : nullptr) {}
    box(box&&) noexcept = default;
    box& operator=(const box& o) {
        if (this != &o) p_ = o.p_ ? std::make_unique<T>(*o.p_) : nullptr;
        return *this;
    }
    box& operator=(box&&) noexcept = default;
    box& operator=(const T& v) {
        p_ = std::make_unique<T>(v);
        return *this;
    }
    box& operator=(T&& v) {
        p_ = std::make_unique<T>(std::move(v));
        return *this;
    }
    ~box() = default;

    [[nodiscard]] bool has_value() const noexcept { return p_ != nullptr; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    T& operator*() noexcept { return *p_; }
    const T& operator*() const noexcept { return *p_; }
    T* operator->() noexcept { return p_.get(); }
    const T* operator->() const noexcept { return p_.get(); }
    T& emplace() {
        p_ = std::make_unique<T>();
        return *p_;
    }
    void reset() noexcept { p_.reset(); }

    friend bool operator==(const box& a, const box& b) {
        if (a.has_value() != b.has_value()) return false;
        return !a.has_value() || *a == *b;
    }

private:
    std::unique_ptr<T> p_;
};

enum class decode_error : std::uint8_t {
    truncated,
    malformed_varint,
    length_overflow,
    malformed_group,
    too_deep,
    bad_wire_type,
    bad_tag,
    invalid_utf8,
};

namespace detail {

[[noreturn]] consteval void fail(std::string_view msg) { throw schema_error(std::define_static_string(msg)); }

consteval std::string to_dec(std::uint64_t v) {
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + v % 10));
        v /= 10;
    } while (v != 0);
    return s;
}

consteval std::string format_error(const schema::error& e) {
    std::string s = "tep: schema error at line " + to_dec(e.loc.line) + ", column " + to_dec(e.loc.column) +
                    ": " + std::string(schema::describe(e.code));
    if (!e.token.empty()) s += " ('" + std::string(e.token) + "')";
    return s;
}

// C++26 keywords and alternative tokens, sorted for binary search.
inline constexpr std::string_view cpp_keywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
    "catch", "char", "char16_t", "char32_t", "char8_t", "class", "co_await", "co_return", "co_yield",
    "compl", "concept", "const", "const_cast", "consteval", "constexpr", "constinit", "continue",
    "contract_assert", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short",
    "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this",
    "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

// Bit n of lengths[c - 'a'] is set when a keyword of length n starts with c. Most field names
// fail this test and skip the search.
struct keyword_index {
    std::uint32_t lengths[26];
};

consteval keyword_index make_keyword_index() {
    keyword_index k{};
    for (const std::string_view w : cpp_keywords) k.lengths[w[0] - 'a'] |= std::uint32_t{1} << w.size();
    return k;
}

inline constexpr keyword_index keyword_lengths = make_keyword_index();

// A field named like a C++ keyword gets a trailing underscore, as protoc's C++ output does.
consteval bool is_keyword(std::string_view n) {
    const char c = n.data()[0];  // names are never empty
    if (c < 'a' || c > 'z' || n.size() > 31 || ((keyword_lengths.lengths[c - 'a'] >> n.size()) & 1u) == 0)
        return false;
    return std::binary_search(std::begin(cpp_keywords), std::end(cpp_keywords), n);
}

}  // namespace detail

// Static, structural tables built once per schema. Indices match schema::file.
struct field_meta {
    std::uint32_t name_offset;  // the C++ member name in schema_tables::names
    std::uint32_t name_size;
    std::uint32_t number;
    schema::field_kind kind;
    schema::scalar type;
    std::uint32_t type_index;  // message or enum index for those kinds
    bool repeated;
    bool packed;
};

// Names and paths are offsets of NUL-terminated strings in schema_tables::names (see table_string):
// all strings are promoted to static storage once, and define_static_array refuses pointers into
// the middle of a promoted string.
struct message_meta {
    std::uint32_t full_name;
    std::uint32_t first_field;
    std::uint32_t field_count;
};

struct enum_meta {
    std::uint32_t full_name;
    std::uint32_t first_value;
    std::uint32_t value_count;
};

struct enum_value_meta {
    std::uint32_t name;
    std::int32_t number;
};

struct rpc_meta {
    std::uint32_t name;  // method name
    std::uint32_t path;  // "/package.Service/Method"
    std::uint32_t request;
    std::uint32_t response;
};

struct service_meta {
    std::uint32_t full_name;
    std::uint32_t first_rpc;
    std::uint32_t rpc_count;
};

struct schema_tables {
    const char* names;  // every C++ member name (offset and size) and every name and path of the metadata
    std::span<const message_meta> messages;
    std::span<const field_meta> fields;
    std::span<const std::uint32_t> definition_order;
    std::span<const enum_meta> enums;
    std::span<const enum_value_meta> enum_values;
    std::span<const service_meta> services;
    std::span<const rpc_meta> rpcs;
};

// A name or path of the metadata, by its offset.
[[nodiscard]] constexpr const char* table_string(const schema_tables& t, std::uint32_t at) noexcept {
    return t.names + at;
}

namespace detail {

consteval schema_tables build_tables(std::string_view text) {
    auto parsed = schema::parse(text);
    if (!parsed) fail(format_error(parsed.error()));
    const schema::file& f = *parsed;

    // Every string of the tables lives in one static string, promoted once: the member names
    // (offset and size) and the NUL-terminated names and rpc paths the metadata points into.
    // Buffers are sized up front and written through pointers: in constant evaluation a push_back
    // that grows costs several times a plain store, and each promotion has a cost of its own.
    std::size_t field_count = 0;
    std::size_t value_count = 0;
    std::size_t rpc_count = 0;
    std::size_t bytes = 0;
    for (const auto& m : f.messages) {
        field_count += m.fields.size();
        bytes += m.full_name.size() + 1;
        for (const auto& fd : m.fields) bytes += fd.name.size() + 1;
    }
    for (const auto& e : f.enums) {
        value_count += e.values.size();
        bytes += e.full_name.size() + 1;
        for (const auto& v : e.values) bytes += v.name.size() + 1;
    }
    for (const auto& s : f.services) {
        rpc_count += s.rpcs.size();
        bytes += s.full_name.size() + 1;
        for (const auto& rp : s.rpcs) bytes += 2 * rp.name.size() + s.full_name.size() + 4;
    }
    std::string strings(bytes, '\0');
    char* const begin = strings.data();
    char* w = begin;
    const auto put = [&](std::string_view v) {
        const auto at = static_cast<std::uint32_t>(w - begin);
        const char* c = v.data();
        for (const char* const e = c + v.size(); c != e; ++c) *w++ = *c;
        return at;
    };
    // Offsets of the terminated strings, in the order the metadata is built below.
    std::vector<std::uint32_t> offsets(f.messages.size() + f.enums.size() + value_count + f.services.size() +
                                       2 * rpc_count);
    std::uint32_t* at = offsets.data();
    std::vector<field_meta> fields(field_count);
    field_meta* out = fields.data();
    for (const auto& m : f.messages) {
        *at++ = put(m.full_name);
        *w++ = '\0';
        // Escaping a keyword cannot make two member names equal: the only collision would be `x`
        // next to `x_`, and the proto3 JSON-name check rejects that pair (both have the JSON name
        // x) before this point.
        for (const auto& fd : m.fields) {
            const std::uint32_t offset = put(fd.name);
            if (is_keyword(fd.name)) *w++ = '_';
            *out++ = {offset, static_cast<std::uint32_t>(w - begin) - offset, fd.number, fd.kind, fd.type,
                      fd.type_index, fd.repeated, fd.packed};
        }
    }
    for (const auto& e : f.enums) {
        *at++ = put(e.full_name);
        *w++ = '\0';
        for (const auto& v : e.values) {
            *at++ = put(v.name);
            *w++ = '\0';
        }
    }
    for (const auto& s : f.services) {
        *at++ = put(s.full_name);
        *w++ = '\0';
        for (const auto& rp : s.rpcs) {
            *at++ = put(rp.name);
            *w++ = '\0';
            *at++ = static_cast<std::uint32_t>(w - begin);  // "/package.Service/Method"
            *w++ = '/';
            (void)put(s.full_name);
            *w++ = '/';
            (void)put(rp.name);
            *w++ = '\0';
        }
    }
    strings.resize(static_cast<std::size_t>(w - begin));

    const std::uint32_t* off = offsets.data();
    std::vector<message_meta> messages(f.messages.size());
    std::uint32_t first = 0;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto count = static_cast<std::uint32_t>(f.messages[i].fields.size());
        messages[i] = {*off++, first, count};
        first += count;
    }
    std::vector<enum_meta> enums(f.enums.size());
    std::vector<enum_value_meta> values(value_count);
    enum_value_meta* vo = values.data();
    for (std::size_t i = 0; i < enums.size(); ++i) {
        const auto& e = f.enums[i];
        enums[i] = {*off++, static_cast<std::uint32_t>(vo - values.data()),
                    static_cast<std::uint32_t>(e.values.size())};
        for (const auto& v : e.values) *vo++ = {*off++, v.number};
    }
    std::vector<service_meta> services(f.services.size());
    std::vector<rpc_meta> rpcs(rpc_count);
    rpc_meta* ro = rpcs.data();
    for (std::size_t i = 0; i < services.size(); ++i) {
        const auto& s = f.services[i];
        services[i] = {*off++, static_cast<std::uint32_t>(ro - rpcs.data()),
                       static_cast<std::uint32_t>(s.rpcs.size())};
        for (const auto& rp : s.rpcs) {
            const std::uint32_t name = *off++;
            *ro++ = {name, *off++, rp.request, rp.response};
        }
    }
    return schema_tables{
        std::define_static_string(strings),
        std::define_static_array(messages),
        std::define_static_array(fields),
        std::define_static_array(f.definition_order),
        std::define_static_array(enums),
        std::define_static_array(values),
        std::define_static_array(services),
        std::define_static_array(rpcs),
    };
}

}  // namespace detail

template <schema_source Schema>
inline constexpr schema_tables tables = detail::build_tables(std::string_view(Schema::text));

// The C++ member name of a field.
[[nodiscard]] constexpr std::string_view member_name(const schema_tables& t, const field_meta& f) noexcept {
    return {t.names + f.name_offset, f.name_size};
}

namespace detail {

template <class T>
consteval std::meta::info type_of() {
    return ^^T;
}

consteval std::meta::info scalar_type(schema::scalar s) {
    using enum schema::scalar;
    switch (s) {
        case double_: return type_of<double>();
        case float_: return type_of<float>();
        case int64:
        case sint64:
        case sfixed64: return type_of<std::int64_t>();
        case uint64:
        case fixed64: return type_of<std::uint64_t>();
        case int32:
        case sint32:
        case sfixed32: return type_of<std::int32_t>();
        case uint32:
        case fixed32: return type_of<std::uint32_t>();
        case bool_: return type_of<bool>();
        case string:
        case bytes: return type_of<std::string>();
    }
    fail("tep: unknown scalar type");
}

template <class Schema>
consteval std::meta::info message_type(std::uint32_t index) {
    return std::meta::substitute(^^message, {^^Schema, std::meta::reflect_constant(std::size_t{index})});
}

// Member types: proto3 scalars map to fixed-width integers, float, double, bool and
// std::string (string and bytes, as in protoc's C++ output); enums to std::int32_t (proto3
// enums are open); a singular message to tep::box<M>; repeated T to std::vector<T>.
template <class Schema>
consteval std::meta::info member_type(const field_meta& f) {
    std::meta::info base{};
    switch (f.kind) {
        case schema::field_kind::scalar: base = scalar_type(f.type); break;
        case schema::field_kind::enumeration: base = type_of<std::int32_t>(); break;
        case schema::field_kind::message: base = message_type<Schema>(f.type_index); break;
    }
    if (f.repeated) return std::meta::substitute(^^std::vector, {base});
    if (f.kind == schema::field_kind::message) return std::meta::substitute(^^box, {base});
    return base;
}

template <class Schema>
consteval std::uint32_t find_message(std::string_view full) {
    for (std::size_t i = 0; i < tables<Schema>.messages.size(); ++i)
        if (std::string_view(table_string(tables<Schema>, tables<Schema>.messages[i].full_name)) == full)
            return static_cast<std::uint32_t>(i);
    fail("tep: the schema has no message named '" + std::string(full) + "'");
}

template <class Schema>
consteval std::uint32_t find_service(std::string_view full) {
    for (std::size_t i = 0; i < tables<Schema>.services.size(); ++i)
        if (std::string_view(table_string(tables<Schema>, tables<Schema>.services[i].full_name)) == full)
            return static_cast<std::uint32_t>(i);
    fail("tep: the schema has no service named '" + std::string(full) + "'");
}

template <class Schema>
consteval std::int32_t find_enum_value(std::string_view full) {
    const auto dot = full.rfind('.');
    if (dot == std::string_view::npos) fail("tep: expected 'package.Enum.VALUE', got '" + std::string(full) + "'");
    const std::string_view enum_name = full.substr(0, dot);
    const std::string_view value_name = full.substr(dot + 1);
    for (const auto& e : tables<Schema>.enums) {
        if (std::string_view(table_string(tables<Schema>, e.full_name)) != enum_name) continue;
        for (std::uint32_t k = 0; k < e.value_count; ++k) {
            const auto& v = tables<Schema>.enum_values[e.first_value + k];
            if (std::string_view(table_string(tables<Schema>, v.name)) == value_name) return v.number;
        }
    }
    fail("tep: the schema has no enum value named '" + std::string(full) + "'");
}

}  // namespace detail

// Defines every message of the schema as an aggregate, dependencies first. Call it once per
// translation unit, from a consteval block: consteval { tep::define_messages<S>(); }
// The defaulted parameter makes the schema tables a constant evaluation of their own, done when
// the call is formed: named only in the body, they would be evaluated inside the consteval
// block and share its operation limit (-fconstexpr-ops-limit counts per outermost evaluation).
template <schema_source Schema, std::size_t = tables<Schema>.messages.size()>
consteval void define_messages() {
    const schema_tables& t = tables<Schema>;
    for (const std::uint32_t idx : t.definition_order) {
        const message_meta& m = t.messages[idx];
        std::vector<std::meta::info> specs;
        specs.reserve(m.field_count);
        for (std::uint32_t k = 0; k < m.field_count; ++k) {
            const field_meta& f = t.fields[m.first_field + k];
            specs.push_back(std::meta::data_member_spec(detail::member_type<Schema>(f), {.name = member_name(t, f)}));
        }
        (void)std::meta::define_aggregate(detail::message_type<Schema>(idx), specs);
    }
}

template <schema_source Schema, fixed_string Name>
using message_t = message<Schema, detail::find_message<Schema>(Name.view())>;

template <schema_source Schema, fixed_string Name>
inline constexpr std::int32_t enum_value = detail::find_enum_value<Schema>(Name.view());

// ---------------------------------------------------------------------------------------------
// Codec

namespace detail {

template <class T>
consteval const message_meta& meta_of() {
    return tables<typename message_traits<T>::schema>.messages[message_traits<T>::index];
}

template <class T>
inline constexpr auto members =
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));

// Member indices in ascending field-number order: the order protobuf serializers emit.
template <class T>
consteval std::vector<std::uint32_t> make_order() {
    const message_meta& m = meta_of<T>();
    const auto& fields = tables<typename message_traits<T>::schema>.fields;
    std::vector<std::uint32_t> idx(m.field_count);
    for (std::uint32_t k = 0; k < m.field_count; ++k) idx[k] = k;
    std::sort(idx.begin(), idx.end(), [&](std::uint32_t a, std::uint32_t b) {
        return fields[m.first_field + a].number < fields[m.first_field + b].number;
    });
    return idx;
}

template <class T>
inline constexpr auto order = std::define_static_array(make_order<T>());

template <class T, std::uint32_t K>
inline constexpr field_meta field_of = tables<typename message_traits<T>::schema>.fields[meta_of<T>().first_field + K];

consteval schema::scalar effective(const field_meta& f) {
    return f.kind == schema::field_kind::enumeration ? schema::scalar::int32 : f.type;
}

consteval wire::wire_type wire_of(schema::scalar s) {
    using enum schema::scalar;
    switch (s) {
        case double_:
        case fixed64:
        case sfixed64: return wire::wire_type::i64;
        case float_:
        case fixed32:
        case sfixed32: return wire::wire_type::i32;
        case string:
        case bytes: return wire::wire_type::len;
        default: return wire::wire_type::varint;
    }
}

consteval wire::wire_type element_wire(const field_meta& f) {
    return f.kind == schema::field_kind::message ? wire::wire_type::len : wire_of(effective(f));
}

template <field_meta F>
inline constexpr std::uint32_t tag_of = wire::make_tag(F.number, F.packed ? wire::wire_type::len : element_wire(F));

template <field_meta F>
inline constexpr std::size_t tag_size = wire::varint_size(tag_of<F>);

template <schema::scalar S, class V>
constexpr std::uint64_t varint_value(V v) noexcept {
    using enum schema::scalar;
    if constexpr (S == int32) {
        return wire::int32_wire(v);
    } else if constexpr (S == sint32) {
        return wire::zigzag32(v);
    } else if constexpr (S == sint64) {
        return wire::zigzag64(v);
    } else if constexpr (S == bool_) {
        return v ? 1u : 0u;
    } else {
        return static_cast<std::uint64_t>(v);  // int64 (two's complement), uint32, uint64
    }
}

template <schema::scalar S, class V>
constexpr std::size_t value_size(const V& v) noexcept {
    constexpr auto wt = wire_of(S);
    if constexpr (wt == wire::wire_type::varint) {
        return wire::varint_size(varint_value<S>(v));
    } else if constexpr (wt == wire::wire_type::i32) {
        return 4;
    } else if constexpr (wt == wire::wire_type::i64) {
        return 8;
    } else {
        return wire::varint_size(v.size()) + v.size();
    }
}

// proto3 implicit presence: a singular scalar equal to its default is not written. Floating
// point compares the bit pattern, so -0.0 is written, as libprotobuf does.
template <schema::scalar S, class V>
constexpr bool is_default(const V& v) noexcept {
    using enum schema::scalar;
    if constexpr (S == string || S == bytes) {
        return v.empty();
    } else if constexpr (S == float_) {
        return std::bit_cast<std::uint32_t>(v) == 0;
    } else if constexpr (S == double_) {
        return std::bit_cast<std::uint64_t>(v) == 0;
    } else {
        return v == V{};
    }
}

template <schema::scalar S, class V>
void write_value(wire::writer& w, const V& v) noexcept {
    using enum schema::scalar;
    constexpr auto wt = wire_of(S);
    if constexpr (wt == wire::wire_type::varint) {
        w.varint(varint_value<S>(v));
    } else if constexpr (S == float_) {
        w.fixed32(std::bit_cast<std::uint32_t>(v));
    } else if constexpr (S == double_) {
        w.fixed64(std::bit_cast<std::uint64_t>(v));
    } else if constexpr (wt == wire::wire_type::i32) {
        w.fixed32(static_cast<std::uint32_t>(v));
    } else if constexpr (wt == wire::wire_type::i64) {
        w.fixed64(static_cast<std::uint64_t>(v));
    } else {
        w.varint(v.size());
        w.bytes(v.data(), v.size());
    }
}

template <schema::scalar S, class Vec>
constexpr std::size_t packed_payload(const Vec& v) noexcept {
    constexpr auto wt = wire_of(S);
    if constexpr (wt == wire::wire_type::i32) {
        return 4 * v.size();
    } else if constexpr (wt == wire::wire_type::i64) {
        return 8 * v.size();
    } else {
        std::size_t n = 0;
        for (const auto& e : v) n += wire::varint_size(varint_value<S>(e));
        return n;
    }
}

}  // namespace detail

template <synthesized_message T>
constexpr std::size_t byte_size(const T& msg) noexcept;

template <synthesized_message T>
std::byte* encode_to(const T& msg, std::byte* out) noexcept;

namespace detail {

template <field_meta F, class V>
constexpr std::size_t field_size(const V& v) noexcept {
    if constexpr (F.kind == schema::field_kind::message) {
        if constexpr (F.repeated) {
            std::size_t n = 0;
            for (const auto& e : v) {
                const std::size_t s = byte_size(e);
                n += tag_size<F> + wire::varint_size(s) + s;
            }
            return n;
        } else {
            if (!v.has_value()) return 0;
            const std::size_t s = byte_size(*v);
            return tag_size<F> + wire::varint_size(s) + s;
        }
    } else {
        constexpr auto S = effective(F);
        if constexpr (F.repeated) {
            if (v.empty()) return 0;
            if constexpr (F.packed) {
                const std::size_t p = packed_payload<S>(v);
                return tag_size<F> + wire::varint_size(p) + p;
            } else {
                std::size_t n = 0;
                for (const auto& e : v) n += tag_size<F> + value_size<S>(e);
                return n;
            }
        } else {
            if (is_default<S>(v)) return 0;
            return tag_size<F> + value_size<S>(v);
        }
    }
}

template <field_meta F, class V>
void write_field(wire::writer& w, const V& v) noexcept {
    if constexpr (F.kind == schema::field_kind::message) {
        const auto one = [&](const auto& e) {
            w.varint(tag_of<F>);
            w.varint(byte_size(e));
            w.p = encode_to(e, w.p);
        };
        if constexpr (F.repeated) {
            for (const auto& e : v) one(e);
        } else if (v.has_value()) {
            one(*v);
        }
    } else {
        constexpr auto S = effective(F);
        if constexpr (F.repeated) {
            if (v.empty()) return;
            if constexpr (F.packed) {
                w.varint(tag_of<F>);
                w.varint(packed_payload<S>(v));
                for (const auto& e : v) write_value<S>(w, e);
            } else {
                for (const auto& e : v) {
                    w.varint(tag_of<F>);
                    write_value<S>(w, e);
                }
            }
        } else {
            if (is_default<S>(v)) return;
            w.varint(tag_of<F>);
            write_value<S>(w, v);
        }
    }
}

}  // namespace detail

template <synthesized_message T>
constexpr std::size_t byte_size(const T& msg) noexcept {
    std::size_t n = 0;
    template for (constexpr std::uint32_t k : detail::order<T>) {
        constexpr auto mem = detail::members<T>[k];
        n += detail::field_size<detail::field_of<T, k>>(msg.[:mem:]);
    }
    return n;
}

// Writes exactly byte_size(msg) bytes at out and returns the end. The caller sizes the buffer.
template <synthesized_message T>
std::byte* encode_to(const T& msg, std::byte* out) noexcept {
    wire::writer w{out};
    template for (constexpr std::uint32_t k : detail::order<T>) {
        constexpr auto mem = detail::members<T>[k];
        detail::write_field<detail::field_of<T, k>>(w, msg.[:mem:]);
    }
    return w.p;
}

template <synthesized_message T>
std::vector<std::byte> encode(const T& msg) {
    std::vector<std::byte> out(byte_size(msg));
    encode_to(msg, out.data());
    return out;
}

// Replaces `out` with the gRPC length-prefixed form of msg. Returns false, leaving `out` empty,
// when the message does not fit the 32-bit length of the prefix.
template <synthesized_message T>
bool encode_framed(const T& msg, std::vector<std::byte>& out) {
    const std::size_t n = byte_size(msg);
    out.clear();
    if (n > 0xFFFF'FFFFu) return false;
    out.resize(grpc::prefix_size + n);
    grpc::write_prefix(out.data(), false, static_cast<std::uint32_t>(n));
    encode_to(msg, out.data() + grpc::prefix_size);
    return true;
}

namespace detail {

constexpr decode_error from_read(wire::read_error e) noexcept {
    switch (e) {
        case wire::read_error::truncated: return decode_error::truncated;
        case wire::read_error::malformed_varint: return decode_error::malformed_varint;
        case wire::read_error::length_overflow: return decode_error::length_overflow;
        case wire::read_error::malformed_group: return decode_error::malformed_group;
        case wire::read_error::too_deep: return decode_error::too_deep;
        case wire::read_error::bad_wire_type: return decode_error::bad_wire_type;
    }
    return decode_error::bad_wire_type;
}

template <schema::scalar S, class V>
constexpr bool read_value(wire::reader& r, V& out, decode_error& err) {
    using enum schema::scalar;
    wire::read_error re{};
    constexpr auto wt = wire_of(S);
    if constexpr (wt == wire::wire_type::varint) {
        std::uint64_t v = 0;
        if (!r.varint(v, re)) return err = from_read(re), false;
        if constexpr (S == sint32) {
            out = wire::unzigzag32(static_cast<std::uint32_t>(v));
        } else if constexpr (S == sint64) {
            out = wire::unzigzag64(v);
        } else if constexpr (S == bool_) {
            out = v != 0;
        } else {
            out = static_cast<V>(v);  // int32 and uint32 keep the low 32 bits, as libprotobuf does
        }
    } else if constexpr (wt == wire::wire_type::i32) {
        std::uint32_t v = 0;
        if (!r.fixed32(v, re)) return err = from_read(re), false;
        if constexpr (S == float_) {
            out = std::bit_cast<float>(v);
        } else {
            out = static_cast<V>(v);
        }
    } else if constexpr (wt == wire::wire_type::i64) {
        std::uint64_t v = 0;
        if (!r.fixed64(v, re)) return err = from_read(re), false;
        if constexpr (S == double_) {
            out = std::bit_cast<double>(v);
        } else {
            out = static_cast<V>(v);
        }
    } else {
        std::span<const std::byte> s;
        if (!r.length_delimited(s, re)) return err = from_read(re), false;
        out.assign(reinterpret_cast<const char*>(s.data()), s.size());
        if constexpr (S == string) {
            if (!wire::valid_utf8(std::string_view(out))) return err = decode_error::invalid_utf8, false;
        }
    }
    return true;
}

}  // namespace detail

template <synthesized_message T>
constexpr bool merge_from(T& msg, wire::reader r, decode_error& err);

namespace detail {

// One occurrence of a known field. A wire type that does not match the field is handled as an
// unknown field (skipped), as libprotobuf does. Repeated numeric fields accept both the packed
// and the unpacked encoding.
template <field_meta F, class V>
constexpr bool read_field(V& v, wire::reader& r, wire::wire_type wt, decode_error& err) {
    wire::read_error re{};
    if constexpr (F.kind == schema::field_kind::message) {
        if (wt != wire::wire_type::len) return r.skip(wt, F.number, re) || (err = from_read(re), false);
        std::span<const std::byte> s;
        if (!r.length_delimited(s, re)) return err = from_read(re), false;
        if constexpr (F.repeated) {
            v.emplace_back();
            return merge_from(v.back(), wire::reader(s), err);
        } else {
            if (!v.has_value()) v.emplace();
            return merge_from(*v, wire::reader(s), err);
        }
    } else {
        constexpr auto S = effective(F);
        constexpr auto ewt = wire_of(S);
        if constexpr (F.repeated) {
            using E = typename V::value_type;
            if constexpr (schema::is_packable(S)) {
                if (wt == wire::wire_type::len) {
                    std::span<const std::byte> s;
                    if (!r.length_delimited(s, re)) return err = from_read(re), false;
                    if constexpr (ewt == wire::wire_type::i32 || ewt == wire::wire_type::i64) {
                        constexpr std::size_t width = ewt == wire::wire_type::i32 ? 4 : 8;
                        if (s.size() % width != 0) return err = decode_error::truncated, false;
                        v.reserve(v.size() + s.size() / width);
                    }
                    wire::reader sub(s);
                    while (!sub.done()) {
                        E e{};
                        if (!read_value<S>(sub, e, err)) return false;
                        v.push_back(e);
                    }
                    return true;
                }
            }
            if (wt != ewt) return r.skip(wt, F.number, re) || (err = from_read(re), false);
            E e{};
            if (!read_value<S>(r, e, err)) return false;
            v.push_back(std::move(e));
            return true;
        } else {
            if (wt != ewt) return r.skip(wt, F.number, re) || (err = from_read(re), false);
            return read_value<S>(r, v, err);
        }
    }
}

// Decoding dispatches on the field number in one of two ways. A message with at most
// inline_dispatch_fields fields compares the number with each field inside merge_from. A larger
// message dispatches through tables, with each field's decoder a function of its own: comparisons
// make decoding quadratic in the field count and merge_from as large as the message for the
// optimizer, while the tables cost a function per field and two static arrays per message, which
// made compiling many small messages slower in the pilot (Laptop request 6). The threshold is a
// design choice, not a measured crossover: the many shape (8 fields per message) and the
// end-to-end messages compare; the wide shape compares at 16 fields and uses tables from 32.
inline constexpr std::size_t inline_dispatch_fields = 16;

template <class T, std::uint32_t K>
constexpr bool read_member(T& msg, wire::reader& r, wire::wire_type wt, decode_error& err) {
    constexpr auto mem = members<T>[K];
    return read_field<field_of<T, K>>(msg.[:mem:], r, wt, err);
}

template <class T>
using member_reader = bool (*)(T&, wire::reader&, wire::wire_type, decode_error&);

template <class T>
inline constexpr auto readers = []<std::size_t... K>(std::index_sequence<K...>) {
    return std::array<member_reader<T>, sizeof...(K)>{&read_member<T, static_cast<std::uint32_t>(K)>...};
}(std::make_index_sequence<members<T>.size()>{});

// Field numbers in ascending order, matching order<T>.
template <class T>
consteval std::vector<std::uint32_t> make_numbers() {
    std::vector<std::uint32_t> v;
    for (const std::uint32_t k : order<T>)
        v.push_back(tables<typename message_traits<T>::schema>.fields[meta_of<T>().first_field + k].number);
    return v;
}

template <class T>
inline constexpr auto numbers = std::define_static_array(make_numbers<T>());

// A direct table indexed by field number when the largest number is small next to the field
// count, as in most schemas; a binary search over the sorted numbers otherwise.
template <class T>
inline constexpr bool dense_numbers = numbers<T>.empty() || numbers<T>.back() <= 4 * numbers<T>.size() + 64;

template <class T>
consteval std::vector<std::uint32_t> make_slots() {
    std::vector<std::uint32_t> s(numbers<T>.empty() ? 1 : numbers<T>.back() + 1, 0);
    for (std::size_t i = 0; i < numbers<T>.size(); ++i) s[numbers<T>[i]] = order<T>[i] + 1;
    return s;
}

template <class T>
inline constexpr auto slots = std::define_static_array(make_slots<T>());

// The member index plus one of the field with this number, or 0 when the message has none.
template <class T>
constexpr std::uint32_t slot_of(std::uint32_t number) noexcept {
    if constexpr (dense_numbers<T>) {
        return number < slots<T>.size() ? slots<T>[number] : 0;
    } else {
        std::size_t lo = 0;
        std::size_t hi = numbers<T>.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (numbers<T>[mid] < number) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo < numbers<T>.size() && numbers<T>[lo] == number ? order<T>[lo] + 1 : 0;
    }
}

}  // namespace detail

// Merges the encoded message in r into msg: later singular values win, repeated values append,
// singular messages merge. Unknown fields are skipped and dropped.
template <synthesized_message T>
constexpr bool merge_from(T& msg, wire::reader r, decode_error& err) {
    while (!r.done()) {
        std::uint64_t tag = 0;
        wire::read_error re{};
        const std::byte* const tag_start = r.p;
        if (!r.varint(tag, re)) return err = detail::from_read(re), false;
        // libprotobuf reads a tag as at most five bytes, with a field number of at least 1.
        if (tag > 0xFFFF'FFFFu || (tag >> 3) == 0 || r.p - tag_start > 5) return err = decode_error::bad_tag, false;
        const auto number = static_cast<std::uint32_t>(tag >> 3);
        const auto wt = static_cast<wire::wire_type>(tag & 7u);
        if constexpr (detail::members<T>.size() <= detail::inline_dispatch_fields) {
            bool known = false;
            template for (constexpr std::uint32_t k : detail::order<T>) {
                constexpr field_meta f = detail::field_of<T, k>;
                if (!known && number == f.number) {
                    known = true;
                    constexpr auto mem = detail::members<T>[k];
                    if (!detail::read_field<f>(msg.[:mem:], r, wt, err)) return false;
                }
            }
            if (!known && !r.skip(wt, number, re)) return err = detail::from_read(re), false;
        } else if (const std::uint32_t slot = detail::slot_of<T>(number); slot != 0) {
            if (!detail::readers<T>[slot - 1](msg, r, wt, err)) return false;
        } else if (!r.skip(wt, number, re)) {
            return err = detail::from_read(re), false;
        }
    }
    return true;
}

template <synthesized_message T>
std::expected<T, decode_error> decode(std::span<const std::byte> bytes) {
    T msg{};
    decode_error err{};
    if (!merge_from(msg, wire::reader(bytes), err)) return std::unexpected(err);
    return msg;
}

// Member-wise equality for synthesized messages, found by argument-dependent lookup.
template <synthesized_message T>
constexpr bool operator==(const T& a, const T& b) {
    bool eq = true;
    template for (constexpr auto mem : detail::members<T>) {
        eq = eq && a.[:mem:] == b.[:mem:];
    }
    return eq;
}

// ---------------------------------------------------------------------------------------------
// Unary endpoints

namespace detail {

consteval std::meta::info find_handler(std::meta::info impl, std::string_view name, std::string_view service) {
    std::meta::info found{};
    int count = 0;
    for (const auto m : std::meta::members_of(impl, std::meta::access_context::unchecked())) {
        if (!std::meta::is_function(m) || !std::meta::has_identifier(m)) continue;
        if (std::meta::identifier_of(m) != name) continue;
        if (!std::meta::is_public(m))
            fail("tep: handler member function '" + std::string(name) + "' for rpc " + std::string(service) + "." +
                 std::string(name) + " is not public");
        found = m;
        ++count;
    }
    if (count == 0)
        fail("tep: handler type '" + std::string(std::meta::display_string_of(impl)) +
             "' has no member function named '" + std::string(name) + "' for rpc " + std::string(service) + "." +
             std::string(name));
    if (count > 1) fail("tep: handler member function '" + std::string(name) + "' is overloaded");
    return found;
}

// Form A:  Response Method(const Request&)
template <class Impl, class Req, class Resp, std::meta::info H>
concept returns_response = requires(Impl& i, const Req& q) {
    { i.[:H:](q) } -> std::same_as<Resp>;
};

// Form B:  tep::grpc::status Method(const Request&, Response&)
template <class Impl, class Req, class Resp, std::meta::info H>
concept fills_response = requires(Impl& i, const Req& q, Resp& p) {
    { i.[:H:](q, p) } -> std::same_as<grpc::status>;
};

consteval std::vector<std::uint32_t> iota(std::uint32_t first, std::uint32_t count) {
    std::vector<std::uint32_t> v(count);
    for (std::uint32_t k = 0; k < count; ++k) v[k] = first + k;
    return v;
}

// Every rpc of the service needs a public, non-overloaded member function of Impl with the
// rpc's name, in form A or form B. Checked when the endpoint type is instantiated.
template <schema_source Schema, fixed_string Service, class Impl>
consteval bool verify_handlers() {
    constexpr service_meta svc = tables<Schema>.services[find_service<Schema>(Service.view())];
    template for (constexpr std::uint32_t k : std::define_static_array(iota(svc.first_rpc, svc.rpc_count))) {
        constexpr rpc_meta r = tables<Schema>.rpcs[k];
        constexpr std::string_view name = table_string(tables<Schema>, r.name);
        constexpr std::meta::info h = find_handler(^^Impl, name, Service.view());
        using Req = message<Schema, r.request>;
        using Resp = message<Schema, r.response>;
        if constexpr (!returns_response<Impl, Req, Resp, h> && !fills_response<Impl, Req, Resp, h>) {
            fail("tep: handler member function '" + std::string(name) + "' must be 'Response " + std::string(name) +
                 "(const Request&)' or 'tep::grpc::status " + std::string(name) +
                 "(const Request&, Response&)' with Request " +
                 std::string(table_string(tables<Schema>, tables<Schema>.messages[r.request].full_name)) +
                 " and Response " +
                 std::string(table_string(tables<Schema>, tables<Schema>.messages[r.response].full_name)));
        }
    }
    return true;
}

}  // namespace detail

// The unary endpoints of one service, bound to a handler object (see verify_handlers).
template <schema_source Schema, fixed_string Service, class Impl>
class endpoint {
public:
    static constexpr std::uint32_t service_index = detail::find_service<Schema>(Service.view());
    static constexpr service_meta service = tables<Schema>.services[service_index];
    static constexpr auto rpc_indices = std::define_static_array(detail::iota(service.first_rpc, service.rpc_count));
    static_assert(detail::verify_handlers<Schema, Service, Impl>());

    explicit endpoint(Impl& impl) noexcept : impl_(impl) {}

    [[nodiscard]] static constexpr std::size_t method_count() noexcept { return service.rpc_count; }
    [[nodiscard]] static constexpr std::string_view path(std::size_t i) noexcept {
        if (i >= service.rpc_count) return {};
        return table_string(tables<Schema>, tables<Schema>.rpcs[service.first_rpc + i].path);
    }

    // Runs one unary call. `request` is the message payload without the gRPC prefix. On ok,
    // `response` holds the length-prefixed response message; otherwise it is empty. An
    // allocation failure is RESOURCE_EXHAUSTED and any other exception from the handler is
    // UNKNOWN, as gRPC servers report them; no exception leaves this function.
    grpc::status invoke(std::string_view method_path, std::span<const std::byte> request,
                        std::vector<std::byte>& response) noexcept {
        grpc::status st = grpc::status::unimplemented;
        try {
            response.clear();
            bool found = false;
            template for (constexpr std::uint32_t k : rpc_indices) {
                constexpr rpc_meta r = tables<Schema>.rpcs[k];
                if (!found && method_path == std::string_view(table_string(tables<Schema>, r.path))) {
                    found = true;
                    st = call<k>(request, response);
                }
            }
        } catch (const std::bad_alloc&) {
            st = grpc::status::resource_exhausted;
        } catch (...) {
            st = grpc::status::unknown;
        }
        if (st != grpc::status::ok) response.clear();
        return st;
    }

    // Runs one unary call from a whole request body (prefix included).
    grpc::status invoke_framed(std::string_view method_path, std::span<const std::byte> body,
                               std::vector<std::byte>& response) noexcept {
        response.clear();
        const auto frame = grpc::parse_unary(body);
        if (!frame) {
            return frame.error() == grpc::frame_error::too_large ? grpc::status::resource_exhausted
                                                                 : grpc::status::internal;
        }
        // No grpc-encoding is negotiated, so a compressed message is a protocol error (INTERNAL),
        // as the gRPC specification and grpc-go treat it.
        if (frame->compressed) return grpc::status::internal;
        return invoke(method_path, frame->payload, response);
    }

private:
    Impl& impl_;


    template <std::uint32_t K>
    grpc::status call(std::span<const std::byte> request, std::vector<std::byte>& response) {
        constexpr rpc_meta r = tables<Schema>.rpcs[K];
        constexpr std::meta::info h =
            detail::find_handler(^^Impl, table_string(tables<Schema>, r.name), Service.view());
        using Req = message<Schema, r.request>;
        using Resp = message<Schema, r.response>;
        Req req{};
        decode_error err{};
        if (!merge_from(req, wire::reader(request), err)) return grpc::status::internal;
        if constexpr (detail::returns_response<Impl, Req, Resp, h>) {
            const Resp resp = impl_.[:h:](req);
            return encode_framed(resp, response) ? grpc::status::ok : grpc::status::resource_exhausted;
        } else {
            Resp resp{};
            const grpc::status st = impl_.[:h:](req, resp);
            if (st != grpc::status::ok) return st;
            return encode_framed(resp, response) ? grpc::status::ok : grpc::status::resource_exhausted;
        }
    }
};

}  // namespace tep
