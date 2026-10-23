// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Portable proto3 front end: lexer, recursive-descent parser, semantic checks and name
// resolution. Plain C++23, usable at run time and in constant evaluation. It never throws:
// the first error is returned with its line and column. The reflection layer turns that error
// into a compile-time diagnostic.
//
// Lifetime: names in the returned file and the token of an error view the source text, so the
// source must outlive them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tep::schema {

inline constexpr std::uint32_t npos = 0xFFFF'FFFFu;
inline constexpr std::uint32_t max_field_number = (1u << 29) - 1;
inline constexpr std::uint32_t first_implementation_number = 19000;
inline constexpr std::uint32_t last_implementation_number = 19999;
inline constexpr std::uint32_t max_nesting = 32;

enum class scalar : std::uint8_t {
    double_, float_, int64, uint64, int32, fixed64, fixed32, bool_,
    string, bytes, uint32, sfixed32, sfixed64, sint32, sint64,
};

enum class field_kind : std::uint8_t { scalar, enumeration, message };

[[nodiscard]] constexpr bool is_packable(scalar s) noexcept {
    return s != scalar::string && s != scalar::bytes;
}

enum class errc : std::uint8_t {
    unexpected_token,
    unexpected_end,
    unterminated_comment,
    unterminated_string,
    invalid_character,
    expected_identifier,
    expected_integer,
    expected_string,
    integer_overflow,
    missing_syntax,
    syntax_not_proto3,
    duplicate_package,
    field_number_out_of_range,
    field_number_in_implementation_range,
    duplicate_field_number,
    duplicate_field_name,
    field_number_reserved,
    field_name_reserved,
    bad_reserved_range,
    reserved_overlap,
    duplicate_reserved_name,
    json_name_conflict,
    duplicate_definition,
    unknown_type,
    not_a_message_type,
    recursive_message,
    empty_enum,
    enum_first_value_not_zero,
    duplicate_enum_value_name,
    enum_value_alias,
    allow_alias_unused,
    enum_value_name_conflict,
    enum_value_out_of_range,
    packed_on_non_packable,
    unsupported_option,
    unsupported_map,
    unsupported_oneof,
    unsupported_optional,
    unsupported_label,
    unsupported_streaming,
    unsupported_import,
    unsupported_extensions,
    unsupported_group,
    unsupported_editions,
    nesting_too_deep,
};

[[nodiscard]] constexpr std::string_view describe(errc e) noexcept {
    switch (e) {
        case errc::unexpected_token: return "unexpected token";
        case errc::unexpected_end: return "unexpected end of schema";
        case errc::unterminated_comment: return "unterminated block comment";
        case errc::unterminated_string: return "unterminated string literal";
        case errc::invalid_character: return "invalid character";
        case errc::expected_identifier: return "expected an identifier";
        case errc::expected_integer: return "expected an integer";
        case errc::expected_string: return "expected a string literal";
        case errc::integer_overflow: return "integer literal out of range";
        case errc::missing_syntax: return "missing 'syntax = \"proto3\";' as the first statement";
        case errc::syntax_not_proto3: return "only syntax \"proto3\" is supported";
        case errc::duplicate_package: return "more than one package statement";
        case errc::field_number_out_of_range: return "field number must be in 1 to 536870911";
        case errc::field_number_in_implementation_range:
            return "field numbers 19000 to 19999 are reserved for the protobuf implementation";
        case errc::duplicate_field_number: return "duplicate field number";
        case errc::duplicate_field_name: return "duplicate field name";
        case errc::field_number_reserved: return "field number is reserved";
        case errc::field_name_reserved: return "field name is reserved";
        case errc::bad_reserved_range: return "reserved range has its start after its end";
        case errc::reserved_overlap: return "reserved ranges overlap";
        case errc::duplicate_reserved_name: return "name is reserved twice";
        case errc::json_name_conflict: return "two fields have the same JSON name";
        case errc::duplicate_definition: return "name is defined twice";
        case errc::unknown_type: return "unknown type name";
        case errc::not_a_message_type: return "rpc request and response types must be messages";
        case errc::recursive_message: return "recursive message types are not supported";
        case errc::empty_enum: return "enum has no values";
        case errc::enum_first_value_not_zero: return "the first enum value must be zero in proto3";
        case errc::duplicate_enum_value_name: return "duplicate enum value name";
        case errc::enum_value_alias: return "two enum values share a number without allow_alias";
        case errc::allow_alias_unused: return "allow_alias is set but no two values share a number";
        case errc::enum_value_name_conflict:
            return "two enum values have the same name once the enum name prefix and case are ignored";
        case errc::enum_value_out_of_range: return "enum value must fit in int32";
        case errc::packed_on_non_packable:
            return "[packed] is only valid on repeated numeric, bool and enum fields";
        case errc::unsupported_option:
            return "unknown or unsupported option (custom options need an import, which is not supported)";
        case errc::unsupported_map: return "map fields are not supported";
        case errc::unsupported_oneof: return "oneof is not supported";
        case errc::unsupported_optional: return "proto3 optional fields are not supported";
        case errc::unsupported_label: return "only the 'repeated' label is supported";
        case errc::unsupported_streaming: return "streaming rpcs are not supported (unary only)";
        case errc::unsupported_import: return "import is not supported";
        case errc::unsupported_extensions: return "extensions are not supported";
        case errc::unsupported_group: return "groups are not supported";
        case errc::unsupported_editions: return "editions are not supported";
        case errc::nesting_too_deep: return "definitions are nested too deeply";
    }
    return "unknown error";
}

struct location {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct error {
    errc code{};
    location loc{};
    std::string_view token{};
};

struct field {
    std::string_view name;
    std::uint32_t number = 0;
    field_kind kind = field_kind::scalar;
    scalar type = scalar::int32;
    std::string type_name;           // as written, for enum and message fields
    std::uint32_t type_index = npos; // resolved index into file::enums or file::messages
    bool repeated = false;
    bool packed = false;             // effective encoding of a repeated field
    std::int8_t packed_option = -1;  // -1 unset, 0 [packed = false], 1 [packed = true]
    location loc{};                  // the field name
    location type_loc{};
    location number_loc{};
    std::string_view type_text;      // the type as written, viewing the source
    std::string_view number_text;
};

struct reserved_range {
    std::int64_t lo = 0;
    std::int64_t hi = 0;  // inclusive
    location loc{};
    std::string_view text;
};

struct reserved_name {
    std::string_view name;
    location loc{};
};

struct message {
    std::string_view name;
    std::string full_name;
    std::uint32_t parent = npos;
    std::vector<field> fields;
    std::vector<reserved_range> reserved_ranges;
    std::vector<reserved_name> reserved_names;
    location loc{};
};

struct enum_value {
    std::string_view name;
    std::int32_t number = 0;
    location loc{};
};

struct enumeration {
    std::string_view name;
    std::string full_name;
    std::uint32_t parent = npos;
    std::vector<enum_value> values;
    std::vector<reserved_range> reserved_ranges;
    std::vector<reserved_name> reserved_names;
    bool allow_alias = false;
    location loc{};
};

struct rpc {
    std::string_view name;
    std::string request_name;   // as written
    std::string response_name;  // as written
    std::uint32_t request = npos;
    std::uint32_t response = npos;
    location loc{};
    location request_loc{};
    location response_loc{};
};

struct service {
    std::string_view name;
    std::string full_name;
    std::vector<rpc> rpcs;
    location loc{};
};

struct file {
    std::string package;
    std::vector<message> messages;
    std::vector<enumeration> enums;
    std::vector<service> services;
    std::vector<std::uint32_t> definition_order;  // message indices, dependencies first

    [[nodiscard]] constexpr std::uint32_t find_message(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < messages.size(); ++i)
            if (messages[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
    [[nodiscard]] constexpr std::uint32_t find_enum(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < enums.size(); ++i)
            if (enums[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
    [[nodiscard]] constexpr std::uint32_t find_service(std::string_view full) const noexcept {
        for (std::size_t i = 0; i < services.size(); ++i)
            if (services[i].full_name == full) return static_cast<std::uint32_t>(i);
        return npos;
    }
};

// ---------------------------------------------------------------------------------------------
// Lexer

enum class tok : std::uint8_t { end, ident, integer, floating, string, symbol };

struct token {
    tok kind = tok::end;
    std::string_view text;  // for strings: the contents between the quotes, escapes not decoded
    location loc{};
};

namespace detail {

[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }
[[nodiscard]] constexpr bool is_hex(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
// A switch, not string_view::find (see split_path in grpc.hpp for the GCC 16.2.1 defect).
[[nodiscard]] constexpr bool is_symbol_char(char c) noexcept {
    switch (c) {
        case '{': case '}': case '(': case ')': case '[': case ']': case ';': case '=':
        case ',': case '<': case '>': case '.': case '-': case '+': case ':': return true;
        default: return false;
    }
}
[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

// Parses a protobuf integer literal (decimal, hex or octal). Returns false on a malformed
// literal; sets overflow when the value does not fit in 64 bits.
[[nodiscard]] constexpr bool parse_integer(std::string_view t, std::uint64_t& out,
                                           bool& overflow) noexcept {
    overflow = false;
    out = 0;
    if (t.empty()) return false;
    std::uint64_t base = 10;
    std::size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (t.size() > 1 && t[0] == '0') {
        base = 8;
        i = 1;
    }
    for (; i < t.size(); ++i) {
        const char c = t[i];
        std::uint64_t d = 0;
        if (base == 16) {
            if (!is_hex(c)) return false;
            d = static_cast<std::uint64_t>(hex_value(c));
        } else {
            if (!is_digit(c)) return false;
            d = static_cast<std::uint64_t>(c - '0');
            if (d >= base) return false;
        }
        if (out > (~std::uint64_t{0} - d) / base) overflow = true;
        out = out * base + d;
    }
    return true;
}

[[nodiscard]] constexpr bool is_integer_literal(std::string_view t) noexcept {
    std::uint64_t v = 0;
    bool overflow = false;
    return parse_integer(t, v, overflow);
}

}  // namespace detail

// Splits the whole source into tokens. One pass; tokens view the source. It walks pointers and
// derives a column from the start of the line, because constant evaluation charges for every
// checked index and every call: this keeps the cost per character small.
[[nodiscard]] constexpr std::expected<std::vector<token>, error> tokenize(std::string_view src) {
    std::vector<token> out;
    out.reserve(src.size() / 4 + 1);
    const char* const end = src.data() + src.size();
    const char* p = src.data();
    const char* line_start = p;
    std::uint32_t line = 1;
    const auto loc_at = [&](const char* q) {
        return location{line, static_cast<std::uint32_t>(q - line_start) + 1};
    };
    const auto view = [](const char* a, const char* b) {
        return std::string_view(a, static_cast<std::size_t>(b - a));
    };
    while (p != end) {
        const char c = *p;
        if (c == '\n') {
            ++p;
            ++line;
            line_start = p;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++p;
            continue;
        }
        if (c == '/' && end - p > 1 && p[1] == '/') {
            while (p != end && *p != '\n') ++p;
            continue;
        }
        if (c == '/' && end - p > 1 && p[1] == '*') {
            const location start = loc_at(p);
            p += 2;
            bool closed = false;
            while (p != end) {
                if (*p == '*' && end - p > 1 && p[1] == '/') {
                    p += 2;
                    closed = true;
                    break;
                }
                if (*p == '\n') {
                    ++line;
                    line_start = p + 1;
                }
                ++p;
            }
            if (!closed) return std::unexpected(error{errc::unterminated_comment, start, "/*"});
            continue;
        }
        const char* const b = p;
        const location start = loc_at(p);
        if (detail::is_alpha(c)) {
            do {
                ++p;
            } while (p != end && detail::is_alnum(*p));
            out.push_back({tok::ident, view(b, p), start});
            continue;
        }
        if (detail::is_digit(c) || (c == '.' && end - p > 1 && detail::is_digit(p[1]))) {
            const bool hex = end - b > 1 && b[0] == '0' && (b[1] == 'x' || b[1] == 'X');
            while (p != end) {
                const char d = *p;
                if (detail::is_alnum(d) || d == '.') {
                    ++p;
                } else if ((d == '+' || d == '-') && !hex && (p[-1] == 'e' || p[-1] == 'E')) {
                    ++p;
                } else {
                    break;
                }
            }
            const std::string_view text = view(b, p);
            out.push_back({detail::is_integer_literal(text) ? tok::integer : tok::floating, text, start});
            continue;
        }
        if (c == '"' || c == '\'') {
            ++p;
            const char* const content = p;
            bool closed = false;
            while (p != end) {
                const char d = *p;
                if (d == '\n') break;
                if (d == '\\') {
                    p += end - p > 1 ? 2 : 1;
                    continue;
                }
                if (d == c) {
                    closed = true;
                    break;
                }
                ++p;
            }
            if (!closed) return std::unexpected(error{errc::unterminated_string, start, view(b, b + 1)});
            out.push_back({tok::string, view(content, p), start});
            ++p;
            continue;
        }
        if (detail::is_symbol_char(c)) {
            ++p;
            out.push_back({tok::symbol, view(b, p), start});
            continue;
        }
        return std::unexpected(error{errc::invalid_character, start, view(b, b + 1)});
    }
    out.push_back({tok::end, {}, loc_at(p)});
    return out;
}

// ---------------------------------------------------------------------------------------------
// Parser

namespace detail {

using namespace std::string_view_literals;

struct scalar_name {
    std::string_view name;
    scalar type;
};

inline constexpr scalar_name scalar_names[] = {
    {"double", scalar::double_},    {"float", scalar::float_},   {"int64", scalar::int64},
    {"uint64", scalar::uint64},     {"int32", scalar::int32},    {"fixed64", scalar::fixed64},
    {"fixed32", scalar::fixed32},   {"bool", scalar::bool_},     {"string", scalar::string},
    {"bytes", scalar::bytes},       {"uint32", scalar::uint32},  {"sfixed32", scalar::sfixed32},
    {"sfixed64", scalar::sfixed64}, {"sint32", scalar::sint32},  {"sint64", scalar::sint64},
};

// Scalar type names start with b, d, f, i, s or u; other names skip the table.
[[nodiscard]] constexpr bool find_scalar(std::string_view n, scalar& out) noexcept {
    if (n.empty()) return false;
    switch (n.front()) {
        case 'b': case 'd': case 'f': case 'i': case 's': case 'u': break;
        default: return false;
    }
    for (const auto& s : scalar_names) {
        if (s.name.front() == n.front() && s.name == n) {
            out = s.type;
            return true;
        }
    }
    return false;
}

class parser {
public:
    constexpr explicit parser(std::vector<token> toks) : toks_(std::move(toks)) {}

    [[nodiscard]] constexpr bool parse_file() {
        if (!parse_syntax()) return false;
        while (!at_end()) {
            const token& t = peek();
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "package"sv)) {
                if (!parse_package()) return false;
            } else if (is_word(t, "import"sv)) {
                return fail(errc::unsupported_import, t);
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::file)) return false;
            } else if (is_word(t, "message"sv)) {
                if (!parse_message(npos, "")) return false;
            } else if (is_word(t, "enum"sv)) {
                if (!parse_enum(npos, "")) return false;
            } else if (is_word(t, "service"sv)) {
                if (!parse_service()) return false;
            } else if (is_word(t, "extend"sv)) {
                return fail(errc::unsupported_extensions, t);
            } else {
                return fail(errc::unexpected_token, t);
            }
        }
        return true;
    }

    [[nodiscard]] constexpr file& result() noexcept { return out_; }
    [[nodiscard]] constexpr const error& failure() const noexcept { return err_; }

private:
    enum class level : std::uint8_t { file, message, field, enumeration, enum_value, service, rpc };

    std::vector<token> toks_;
    std::size_t i_ = 0;
    file out_{};
    error err_{};
    std::uint32_t depth_ = 0;
    bool have_package_ = false;

    [[nodiscard]] constexpr const token& peek(std::size_t ahead = 0) const noexcept {
        const std::size_t k = i_ + ahead;
        return k < toks_.size() ? toks_[k] : toks_.back();
    }
    [[nodiscard]] constexpr bool at_end() const noexcept { return peek().kind == tok::end; }
    // The source text from the start of token a to the end of token b.
    [[nodiscard]] constexpr std::string_view span(std::size_t a, std::size_t b) const noexcept {
        const std::string_view x = toks_[a].text;
        const std::string_view y = toks_[b].text;
        return {x.data(), static_cast<std::size_t>(y.data() + y.size() - x.data())};
    }
    [[nodiscard]] static constexpr bool is_symbol(const token& t, char c) noexcept {
        return t.kind == tok::symbol && t.text.size() == 1 && t.text[0] == c;
    }
    [[nodiscard]] static constexpr bool is_word(const token& t, std::string_view w) noexcept {
        return t.kind == tok::ident && t.text == w;
    }
    constexpr bool fail(errc code, const token& t) {
        err_ = error{t.kind == tok::end ? errc::unexpected_end : code, t.loc, t.text};
        return false;
    }
    constexpr bool fail_at(errc code, location loc, std::string_view text) {
        err_ = error{code, loc, text};
        return false;
    }
    [[nodiscard]] constexpr bool expect_symbol(char c) {
        if (!is_symbol(peek(), c)) return fail(errc::unexpected_token, peek());
        ++i_;
        return true;
    }
    [[nodiscard]] constexpr bool expect_ident(std::string_view& out) {
        const token& t = peek();
        if (t.kind != tok::ident) return fail(errc::expected_identifier, t);
        out = t.text;
        ++i_;
        return true;
    }
    [[nodiscard]] constexpr bool expect_integer(std::uint64_t& out) {
        const token& t = peek();
        if (t.kind != tok::integer) return fail(errc::expected_integer, t);
        bool overflow = false;
        (void)parse_integer(t.text, out, overflow);
        if (overflow) return fail(errc::integer_overflow, t);
        ++i_;
        return true;
    }
    // ['.'] ident ('.' ident)*
    [[nodiscard]] constexpr bool parse_type_name(std::string& out) {
        out.clear();
        if (is_symbol(peek(), '.')) {
            out.push_back('.');
            ++i_;
        }
        std::string_view part;
        if (!expect_ident(part)) return false;
        out.append(part);
        while (is_symbol(peek(), '.')) {
            ++i_;
            if (!expect_ident(part)) return false;
            out.push_back('.');
            out.append(part);
        }
        return true;
    }

    [[nodiscard]] constexpr bool parse_syntax() {
        const token& t = peek();
        if (is_word(t, "edition"sv)) return fail(errc::unsupported_editions, t);
        if (!is_word(t, "syntax"sv)) return fail(errc::missing_syntax, t);
        ++i_;
        if (!expect_symbol('=')) return false;
        const token& v = peek();
        if (v.kind != tok::string) return fail(errc::expected_string, v);
        if (v.text != "proto3"sv) return fail(errc::syntax_not_proto3, v);
        ++i_;
        return expect_symbol(';');
    }

    [[nodiscard]] constexpr bool parse_package() {
        const token& kw = peek();
        if (have_package_) return fail(errc::duplicate_package, kw);
        ++i_;
        if (is_symbol(peek(), '.')) return fail(errc::unexpected_token, peek());
        std::string name;
        if (!parse_type_name(name)) return false;
        out_.package = std::move(name);
        have_package_ = true;
        return expect_symbol(';');
    }

    // A constant: identifier, signed number, one or more adjacent strings, or a {...} aggregate.
    [[nodiscard]] constexpr bool skip_constant() {
        const token& t = peek();
        if (t.kind == tok::ident || t.kind == tok::integer || t.kind == tok::floating) {
            ++i_;
            return true;
        }
        if (is_symbol(t, '-') || is_symbol(t, '+')) {
            ++i_;
            const token& n = peek();
            if (n.kind != tok::integer && n.kind != tok::floating && n.kind != tok::ident)
                return fail(errc::unexpected_token, n);
            ++i_;
            return true;
        }
        if (t.kind == tok::string) {
            while (peek().kind == tok::string) ++i_;
            return true;
        }
        if (is_symbol(t, '{')) {
            std::uint32_t open = 0;
            do {
                const token& k = peek();
                if (k.kind == tok::end) return fail(errc::unexpected_end, k);
                if (is_symbol(k, '{')) ++open;
                if (is_symbol(k, '}')) --open;
                ++i_;
            } while (open != 0);
            return true;
        }
        return fail(errc::unexpected_token, t);
    }

    // optionName: ident ('.' ident)* | '(' ['.'] ident ('.' ident)* ')' ('.' ident)*
    // `name` views the whole name as written; `custom` is set for a parenthesized name.
    [[nodiscard]] constexpr bool parse_option_name(std::string_view& name, location& loc, bool& custom) {
        const std::size_t first = i_;
        loc = peek().loc;
        custom = false;
        if (is_symbol(peek(), '(')) {
            custom = true;
            ++i_;
            std::string ignored;
            if (!parse_type_name(ignored)) return false;
            if (!expect_symbol(')')) return false;
        } else {
            std::string_view part;
            if (!expect_ident(part)) return false;
        }
        while (is_symbol(peek(), '.')) {
            ++i_;
            std::string_view part;
            if (!expect_ident(part)) return false;
        }
        name = span(first, i_ - 1);
        return true;
    }

    // Options from descriptor.proto, per level. Options that change the wire format
    // (message_set_wire_format, map_entry, default) are left out and so refused. A custom option
    // needs an import, which is not supported, so it is refused as protoc would refuse it.
    [[nodiscard]] static constexpr bool known_option(level lv, std::string_view name) noexcept {
        static constexpr std::string_view file_options[] = {
            "java_package", "java_outer_classname", "java_multiple_files", "java_generate_equals_and_hash",
            "java_string_check_utf8", "optimize_for", "go_package", "cc_generic_services", "java_generic_services",
            "py_generic_services", "deprecated", "cc_enable_arenas", "objc_class_prefix", "csharp_namespace",
            "swift_prefix", "php_class_prefix", "php_namespace", "php_metadata_namespace", "ruby_package"};
        static constexpr std::string_view message_options[] = {"no_standard_descriptor_accessor", "deprecated",
                                                        "deprecated_legacy_json_field_conflicts"};
        static constexpr std::string_view field_options[] = {"ctype", "packed", "jstype", "lazy", "unverified_lazy",
                                                      "deprecated", "weak", "debug_redact", "retention", "targets",
                                                      "json_name"};
        static constexpr std::string_view enum_options[] = {"allow_alias", "deprecated",
                                                     "deprecated_legacy_json_field_conflicts"};
        static constexpr std::string_view enum_value_options[] = {"deprecated", "debug_redact"};
        static constexpr std::string_view service_options[] = {"deprecated"};
        static constexpr std::string_view rpc_options[] = {"deprecated", "idempotency_level"};
        const auto in = [&](const auto& list) {
            for (const std::string_view o : list)
                if (o == name) return true;
            return false;
        };
        switch (lv) {
            case level::file: return in(file_options);
            case level::message: return in(message_options);
            case level::field: return in(field_options);
            case level::enumeration: return in(enum_options);
            case level::enum_value: return in(enum_value_options);
            case level::service: return in(service_options);
            case level::rpc: return in(rpc_options);
        }
        return false;
    }

    [[nodiscard]] constexpr bool check_option_name(level lv, std::string_view name, location loc, bool custom) {
        if (name.starts_with("features"sv)) return fail_at(errc::unsupported_editions, loc, name);
        if (custom || !known_option(lv, name)) return fail_at(errc::unsupported_option, loc, name);
        return true;
    }

    // `option name = constant;` at file, message, enum, service or rpc level.
    [[nodiscard]] constexpr bool parse_option(level lv, bool* allow_alias = nullptr) {
        ++i_;  // 'option'
        std::string_view name;
        location loc{};
        bool custom = false;
        if (!parse_option_name(name, loc, custom)) return false;
        if (!check_option_name(lv, name, loc, custom)) return false;
        if (!expect_symbol('=')) return false;
        const token& value = peek();
        if (lv == level::enumeration && name == "allow_alias"sv) {
            if (!is_word(value, "true"sv) && !is_word(value, "false"sv)) return fail(errc::unexpected_token, value);
            if (allow_alias != nullptr) *allow_alias = is_word(value, "true"sv);
        }
        if (!skip_constant()) return false;
        return expect_symbol(';');
    }

    // '[' option (',' option)* ']' on a field or an enum value. Only `packed` changes the wire
    // format.
    [[nodiscard]] constexpr bool parse_field_options(field& f, level lv) {
        ++i_;  // '['
        for (;;) {
            std::string_view name;
            location loc{};
            bool custom = false;
            if (!parse_option_name(name, loc, custom)) return false;
            if (!check_option_name(lv, name, loc, custom)) return false;
            if (!expect_symbol('=')) return false;
            const token& value = peek();
            if (name == "packed"sv) {
                if (f.packed_option != -1) return fail_at(errc::unsupported_option, loc, name);
                if (is_word(value, "true"sv)) {
                    f.packed_option = 1;
                } else if (is_word(value, "false"sv)) {
                    f.packed_option = 0;
                } else {
                    return fail(errc::unexpected_token, value);
                }
            }
            if (!skip_constant()) return false;
            if (is_symbol(peek(), ',')) {
                ++i_;
                continue;
            }
            return expect_symbol(']');
        }
    }

    [[nodiscard]] constexpr bool read_reserved_bound(std::int64_t& v, bool is_enum) {
        const std::int64_t max_value = is_enum ? 2147483647 : std::int64_t{max_field_number};
        bool negative = false;
        if (is_enum && is_symbol(peek(), '-')) {
            negative = true;
            ++i_;
        }
        const token& t = peek();
        std::uint64_t u = 0;
        if (!expect_integer(u)) return false;
        if (u > static_cast<std::uint64_t>(max_value) + (negative ? 1u : 0u) || (!is_enum && u == 0))
            return fail_at(is_enum ? errc::enum_value_out_of_range : errc::field_number_out_of_range, t.loc,
                           t.text);
        v = negative ? -static_cast<std::int64_t>(u) : static_cast<std::int64_t>(u);
        return true;
    }

    // reserved 1, 2, 9 to 11, 40 to max;   or   reserved "a", "b";
    [[nodiscard]] constexpr bool parse_reserved(std::vector<reserved_range>& ranges,
                                                std::vector<reserved_name>& names, bool is_enum) {
        ++i_;  // 'reserved'
        if (peek().kind == tok::string) {
            for (;;) {
                const token& s = peek();
                if (s.kind != tok::string) return fail(errc::expected_string, s);
                names.push_back({s.text, s.loc});
                ++i_;
                if (!is_symbol(peek(), ',')) break;
                ++i_;
            }
            return expect_symbol(';');
        }
        for (;;) {
            const std::size_t first = i_;
            reserved_range r{};
            r.loc = peek().loc;
            if (!read_reserved_bound(r.lo, is_enum)) return false;
            r.hi = r.lo;
            if (is_word(peek(), "to"sv)) {
                ++i_;
                if (is_word(peek(), "max"sv)) {
                    r.hi = is_enum ? 2147483647 : std::int64_t{max_field_number};
                    ++i_;
                } else if (!read_reserved_bound(r.hi, is_enum)) {
                    return false;
                }
            }
            r.text = span(first, i_ - 1);
            if (r.lo > r.hi) return fail_at(errc::bad_reserved_range, r.loc, r.text);
            ranges.push_back(r);
            if (!is_symbol(peek(), ',')) break;
            ++i_;
        }
        return expect_symbol(';');
    }

    [[nodiscard]] constexpr bool parse_field(std::uint32_t msg, bool repeated) {
        field f{};
        f.repeated = repeated;
        const token& type_tok = peek();
        if (is_word(type_tok, "group"sv)) return fail(errc::unsupported_group, type_tok);
        f.type_loc = type_tok.loc;
        const std::size_t type_first = i_;
        if (type_tok.kind == tok::ident && find_scalar(type_tok.text, f.type)) {
            f.kind = field_kind::scalar;
            ++i_;
        } else {
            f.kind = field_kind::message;  // provisional; resolution may make it an enum
            if (!parse_type_name(f.type_name)) return false;
        }
        f.type_text = span(type_first, i_ - 1);
        const token& name_tok = peek();
        if (!expect_ident(f.name)) return false;
        f.loc = name_tok.loc;
        if (!expect_symbol('=')) return false;
        const token& num_tok = peek();
        f.number_loc = num_tok.loc;
        f.number_text = num_tok.text;
        std::uint64_t number = 0;
        if (!expect_integer(number)) return false;
        if (number == 0 || number > max_field_number)
            return fail_at(errc::field_number_out_of_range, num_tok.loc, num_tok.text);
        f.number = static_cast<std::uint32_t>(number);
        if (is_symbol(peek(), '[') && !parse_field_options(f, level::field)) return false;
        if (!expect_symbol(';')) return false;
        out_.messages[msg].fields.push_back(std::move(f));
        return true;
    }

    [[nodiscard]] constexpr bool parse_message_item(std::uint32_t index, const std::string& local) {
        const token& t = peek();
        if (is_symbol(t, ';')) {
            ++i_;
            return true;
        }
        // Only words starting with e, m, o or r can be keywords here. Scalar types and most type
        // names go straight to the field, without a keyword comparison.
        const char first = t.kind == tok::ident ? t.text.front() : '\0';
        if (first == 'e' || first == 'm' || first == 'o' || first == 'r') {
            if (is_word(t, "message"sv)) return parse_message(index, local);
            if (is_word(t, "enum"sv)) return parse_enum(index, local);
            if (is_word(t, "reserved"sv))
                return parse_reserved(out_.messages[index].reserved_ranges, out_.messages[index].reserved_names,
                                      false);
            if (is_word(t, "option"sv)) return parse_option(level::message);
            if (is_word(t, "oneof"sv)) return fail(errc::unsupported_oneof, t);
            if (is_word(t, "map"sv) && is_symbol(peek(1), '<')) return fail(errc::unsupported_map, t);
            if (is_word(t, "optional"sv)) return fail(errc::unsupported_optional, t);
            if (is_word(t, "required"sv)) return fail(errc::unsupported_label, t);
            if (is_word(t, "extensions"sv) || is_word(t, "extend"sv)) return fail(errc::unsupported_extensions, t);
            if (is_word(t, "repeated"sv)) {
                ++i_;
                return parse_field(index, true);
            }
        }
        return parse_field(index, false);
    }

    [[nodiscard]] constexpr bool parse_message(std::uint32_t parent, std::string_view scope) {
        const token& kw = peek();
        ++i_;  // 'message'
        if (depth_ >= max_nesting) return fail(errc::nesting_too_deep, kw);
        std::string_view name;
        const token& name_tok = peek();
        if (!expect_ident(name)) return false;
        if (!expect_symbol('{')) return false;
        const auto index = static_cast<std::uint32_t>(out_.messages.size());
        message m{};
        m.name = name;
        m.full_name = scope.empty() ? std::string(name) : std::string(scope) + "." + std::string(name);
        m.parent = parent;
        m.loc = name_tok.loc;
        out_.messages.push_back(std::move(m));
        const std::string local = out_.messages[index].full_name;
        ++depth_;
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (!parse_message_item(index, local)) return false;
        }
        --depth_;
        return true;
    }

    [[nodiscard]] constexpr bool parse_enum_value(enumeration& e) {
        const token& t = peek();
        enum_value v{};
        v.loc = t.loc;
        if (!expect_ident(v.name)) return false;
        if (!expect_symbol('=')) return false;
        bool negative = false;
        if (is_symbol(peek(), '-')) {
            negative = true;
            ++i_;
        }
        const token& num_tok = peek();
        std::uint64_t u = 0;
        if (!expect_integer(u)) return false;
        if (u > (negative ? 2147483648ull : 2147483647ull))
            return fail_at(errc::enum_value_out_of_range, num_tok.loc, num_tok.text);
        v.number = negative ? static_cast<std::int32_t>(-static_cast<std::int64_t>(u)) : static_cast<std::int32_t>(u);
        if (is_symbol(peek(), '[')) {
            field ignored{};
            if (!parse_field_options(ignored, level::enum_value)) return false;
        }
        if (!expect_symbol(';')) return false;
        e.values.push_back(v);
        return true;
    }

    [[nodiscard]] constexpr bool parse_enum(std::uint32_t parent, std::string_view scope) {
        const token& kw = peek();
        ++i_;  // 'enum'
        if (depth_ >= max_nesting) return fail(errc::nesting_too_deep, kw);
        enumeration e{};
        const token& name_tok = peek();
        if (!expect_ident(e.name)) return false;
        e.full_name = scope.empty() ? std::string(e.name) : std::string(scope) + "." + std::string(e.name);
        e.parent = parent;
        e.loc = name_tok.loc;
        if (!expect_symbol('{')) return false;
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::enumeration, &e.allow_alias)) return false;
            } else if (is_word(t, "reserved"sv)) {
                if (!parse_reserved(e.reserved_ranges, e.reserved_names, true)) return false;
            } else if (!parse_enum_value(e)) {
                return false;
            }
        }
        out_.enums.push_back(std::move(e));
        return true;
    }

    [[nodiscard]] constexpr bool parse_rpc_type(std::string& name, location& loc) {
        if (!expect_symbol('(')) return false;
        const token& t = peek();
        if (is_word(t, "stream"sv) && (peek(1).kind == tok::ident || is_symbol(peek(1), '.')))
            return fail(errc::unsupported_streaming, t);
        loc = t.loc;
        if (!parse_type_name(name)) return false;
        return expect_symbol(')');
    }

    [[nodiscard]] constexpr bool parse_rpc(service& s) {
        ++i_;  // 'rpc'
        rpc r{};
        r.loc = peek().loc;
        if (!expect_ident(r.name)) return false;
        if (!parse_rpc_type(r.request_name, r.request_loc)) return false;
        const token& ret = peek();
        if (!is_word(ret, "returns"sv)) return fail(errc::unexpected_token, ret);
        ++i_;
        if (!parse_rpc_type(r.response_name, r.response_loc)) return false;
        if (is_symbol(peek(), '{')) {
            ++i_;
            for (;;) {
                const token& b = peek();
                if (b.kind == tok::end) return fail(errc::unexpected_end, b);
                if (is_symbol(b, '}')) {
                    ++i_;
                    break;
                }
                if (is_symbol(b, ';')) {
                    ++i_;
                } else if (is_word(b, "option"sv)) {
                    if (!parse_option(level::rpc)) return false;
                } else {
                    return fail(errc::unexpected_token, b);
                }
            }
        } else if (!expect_symbol(';')) {
            return false;
        }
        s.rpcs.push_back(std::move(r));
        return true;
    }

    [[nodiscard]] constexpr bool parse_service() {
        ++i_;  // 'service'
        service s{};
        const token& name_tok = peek();
        if (!expect_ident(s.name)) return false;
        s.full_name = std::string(s.name);
        s.loc = name_tok.loc;
        if (!expect_symbol('{')) return false;
        for (;;) {
            const token& t = peek();
            if (t.kind == tok::end) return fail(errc::unexpected_end, t);
            if (is_symbol(t, '}')) {
                ++i_;
                break;
            }
            if (is_symbol(t, ';')) {
                ++i_;
            } else if (is_word(t, "option"sv)) {
                if (!parse_option(level::service)) return false;
            } else if (is_word(t, "rpc"sv)) {
                if (!parse_rpc(s)) return false;
            } else {
                return fail(errc::unexpected_token, t);
            }
        }
        out_.services.push_back(std::move(s));
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// Semantic checks

enum class symbol : std::uint8_t { message, enumeration, service, package };

struct name_entry {
    std::string_view full;  // views a full_name string or the package, both owned by the file
    symbol kind = symbol::message;
    std::uint32_t index = npos;
};

[[nodiscard]] constexpr bool is_type(symbol k) noexcept { return k == symbol::message || k == symbol::enumeration; }

// A name inside one scope (a message, the file, or a service), for collision checks.
struct scoped_name {
    std::uint32_t scope = 0;
    std::string_view name;  // views the source
    location loc{};
};

[[nodiscard]] constexpr bool before(location a, location b) noexcept {
    return a.line != b.line ? a.line < b.line : a.column < b.column;
}

[[nodiscard]] constexpr char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr char upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr std::uint32_t fnv1a(std::string_view s) noexcept {
    std::uint32_t h = 2166136261u;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

// The index of the first field whose name an earlier field already has, or npos. One pass over
// an open-addressing hash set, so the cost stays linear in constant evaluation.
[[nodiscard]] constexpr std::uint32_t first_duplicate_name(const std::vector<field>& fields,
                                                           std::vector<std::uint32_t>& slots) {
    std::size_t size = 16;
    while (size < 2 * fields.size()) size *= 2;
    slots.assign(size, 0);
    const std::size_t mask = size - 1;
    for (std::uint32_t k = 0; k < fields.size(); ++k) {
        std::size_t i = fnv1a(fields[k].name) & mask;
        while (slots[i] != 0) {
            if (fields[slots[i] - 1].name == fields[k].name) return k;
            i = (i + 1) & mask;
        }
        slots[i] = k + 1;
    }
    return npos;
}

[[nodiscard]] constexpr bool in_ranges(const std::vector<reserved_range>& rs, std::int64_t v) noexcept {
    for (const auto& r : rs)
        if (v >= r.lo && v <= r.hi) return true;
    return false;
}

[[nodiscard]] constexpr bool reserved(const std::vector<reserved_name>& names, std::string_view n) noexcept {
    for (const auto& x : names)
        if (x.name == n) return true;
    return false;
}

// protoc's default JSON name: each underscore is dropped and the next character capitalized.
[[nodiscard]] constexpr std::string json_name(std::string_view n) {
    std::string out;
    bool up = false;
    for (const char c : n) {
        if (c == '_') {
            up = true;
            continue;
        }
        out.push_back(up ? upper(c) : c);
        up = false;
    }
    return out;
}

// protoc's proto3 enum check: remove the enum name as a prefix (ignoring case and underscores)
// when something is left, then convert to PascalCase.
[[nodiscard]] constexpr std::string enum_value_key(std::string_view enum_name, std::string_view value) {
    std::size_t i = 0;
    std::size_t j = 0;
    bool strip = true;
    for (; i < value.size() && j < enum_name.size(); ++i) {
        if (value[i] == '_') continue;
        while (j < enum_name.size() && enum_name[j] == '_') ++j;
        if (j == enum_name.size()) break;
        if (lower(value[i]) != lower(enum_name[j++])) {
            strip = false;
            break;
        }
    }
    while (j < enum_name.size() && enum_name[j] == '_') ++j;
    if (j < enum_name.size()) strip = false;
    while (strip && i < value.size() && value[i] == '_') ++i;
    if (!strip || i == value.size()) i = 0;
    std::string out;
    bool up = true;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') {
            up = true;
            continue;
        }
        out.push_back(up ? upper(value[i]) : lower(value[i]));
        up = false;
    }
    return out;
}

class checker {
public:
    constexpr explicit checker(file& f) : f_(f) {}

    [[nodiscard]] constexpr bool run() {
        if (!check_messages() || !check_enums() || !check_scopes()) return false;
        qualify();
        build_names();
        return resolve() && check_packed() && order_messages();
    }

    [[nodiscard]] constexpr const error& failure() const noexcept { return err_; }

private:
    file& f_;
    error err_{};
    std::vector<name_entry> names_;  // messages, enums, services and package prefixes, by full name

    constexpr bool fail(errc code, location loc, std::string_view token) {
        err_ = error{code, loc, token};
        return false;
    }

    [[nodiscard]] constexpr bool check_reserved(const std::vector<reserved_range>& ranges,
                                                const std::vector<reserved_name>& names) {
        for (std::size_t b = 1; b < ranges.size(); ++b)
            for (std::size_t a = 0; a < b; ++a)
                if (ranges[a].lo <= ranges[b].hi && ranges[b].lo <= ranges[a].hi)
                    return fail(errc::reserved_overlap, ranges[b].loc, ranges[b].text);
        for (std::size_t b = 1; b < names.size(); ++b)
            for (std::size_t a = 0; a < b; ++a)
                if (names[a].name == names[b].name)
                    return fail(errc::duplicate_reserved_name, names[b].loc, names[b].name);
        return true;
    }

    [[nodiscard]] constexpr bool check_json_names(const message& m) {
        bool any_underscore = false;
        for (const field& fd : m.fields)
            for (const char c : fd.name) any_underscore = any_underscore || c == '_';
        if (!any_underscore) return true;  // then every JSON name is the field name, already unique
        std::vector<std::pair<std::string, std::uint32_t>> json;
        json.reserve(m.fields.size());
        for (std::uint32_t k = 0; k < m.fields.size(); ++k) json.emplace_back(json_name(m.fields[k].name), k);
        std::sort(json.begin(), json.end());
        for (std::size_t k = 1; k < json.size(); ++k) {
            if (json[k].first == json[k - 1].first) {
                const field& fd = m.fields[std::max(json[k].second, json[k - 1].second)];
                return fail(errc::json_name_conflict, fd.loc, fd.name);
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool check_message(const message& m,
                                               std::vector<std::pair<std::uint32_t, std::uint32_t>>& numbers,
                                               std::vector<std::uint32_t>& slots) {
        for (const field& fd : m.fields) {
            if (fd.number >= first_implementation_number && fd.number <= last_implementation_number)
                return fail(errc::field_number_in_implementation_range, fd.number_loc, fd.number_text);
            if (in_ranges(m.reserved_ranges, fd.number))
                return fail(errc::field_number_reserved, fd.number_loc, fd.number_text);
            if (reserved(m.reserved_names, fd.name)) return fail(errc::field_name_reserved, fd.loc, fd.name);
        }
        // Field numbers usually increase in declaration order, and then none repeats.
        bool increasing = true;
        for (std::size_t k = 1; k < m.fields.size() && increasing; ++k)
            increasing = m.fields[k - 1].number < m.fields[k].number;
        if (!increasing) {
            numbers.clear();
            for (std::uint32_t k = 0; k < m.fields.size(); ++k) numbers.emplace_back(m.fields[k].number, k);
            std::sort(numbers.begin(), numbers.end());
            for (std::size_t k = 1; k < numbers.size(); ++k) {
                if (numbers[k].first == numbers[k - 1].first) {
                    const field& fd = m.fields[std::max(numbers[k].second, numbers[k - 1].second)];
                    return fail(errc::duplicate_field_number, fd.number_loc, fd.number_text);
                }
            }
        }
        if (const std::uint32_t later = first_duplicate_name(m.fields, slots); later != npos) {
            const field& fd = m.fields[later];
            return fail(errc::duplicate_field_name, fd.loc, fd.name);
        }
        return check_reserved(m.reserved_ranges, m.reserved_names) && check_json_names(m);
    }

    [[nodiscard]] constexpr bool check_messages() {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> numbers;
        std::vector<std::uint32_t> slots;
        for (const auto& m : f_.messages)
            if (!check_message(m, numbers, slots)) return false;
        return true;
    }

    [[nodiscard]] constexpr bool check_enum(const enumeration& e) {
        if (e.values.empty()) return fail(errc::empty_enum, e.loc, e.name);
        if (e.values.front().number != 0)
            return fail(errc::enum_first_value_not_zero, e.values.front().loc, e.values.front().name);
        bool aliased = false;
        for (std::size_t a = 0; a < e.values.size(); ++a) {
            const enum_value& v = e.values[a];
            if (in_ranges(e.reserved_ranges, v.number)) return fail(errc::field_number_reserved, v.loc, v.name);
            if (reserved(e.reserved_names, v.name)) return fail(errc::field_name_reserved, v.loc, v.name);
            for (std::size_t b = 0; b < a; ++b) {
                if (e.values[b].name == v.name) return fail(errc::duplicate_enum_value_name, v.loc, v.name);
                if (e.values[b].number == v.number) {
                    if (!e.allow_alias) return fail(errc::enum_value_alias, v.loc, v.name);
                    aliased = true;
                }
            }
        }
        if (e.allow_alias && !aliased) return fail(errc::allow_alias_unused, e.loc, e.name);
        if (!check_reserved(e.reserved_ranges, e.reserved_names)) return false;
        // Values that collide once the prefix and case are ignored, unless they are aliases.
        std::vector<std::pair<std::string, std::uint32_t>> keys;
        keys.reserve(e.values.size());
        for (std::uint32_t k = 0; k < e.values.size(); ++k)
            keys.emplace_back(enum_value_key(e.name, e.values[k].name), k);
        std::sort(keys.begin(), keys.end());
        for (std::size_t k = 1; k < keys.size(); ++k) {
            if (keys[k].first != keys[k - 1].first) continue;
            const enum_value& x = e.values[keys[k - 1].second];
            const enum_value& y = e.values[keys[k].second];
            if (x.number == y.number) continue;
            const enum_value& later = keys[k].second > keys[k - 1].second ? y : x;
            return fail(errc::enum_value_name_conflict, later.loc, later.name);
        }
        return true;
    }

    [[nodiscard]] constexpr bool check_enums() {
        for (const auto& e : f_.enums)
            if (!check_enum(e)) return false;
        return true;
    }

    // C++-like scoping, as protoc does it: in one scope, nested messages, enums, enum values (in
    // the scope that holds their enum), services and fields share one namespace; rpc names
    // share one per service. Fields enter only scopes that hold another kind of name, since
    // duplicate field names were checked already.
    [[nodiscard]] constexpr bool check_scopes() {
        const auto file_scope = static_cast<std::uint32_t>(f_.messages.size());
        const auto scope_of = [&](std::uint32_t parent) { return parent == npos ? file_scope : parent; };
        std::vector<scoped_name> names;
        std::vector<std::uint8_t> crowded(f_.messages.size(), 0);
        for (const auto& m : f_.messages) {
            names.push_back({scope_of(m.parent), m.name, m.loc});
            if (m.parent != npos) crowded[m.parent] = 1;
        }
        for (const auto& e : f_.enums) {
            names.push_back({scope_of(e.parent), e.name, e.loc});
            for (const auto& v : e.values) names.push_back({scope_of(e.parent), v.name, v.loc});
            if (e.parent != npos) crowded[e.parent] = 1;
        }
        for (std::uint32_t i = 0; i < f_.messages.size(); ++i)
            if (crowded[i] != 0)
                for (const auto& fd : f_.messages[i].fields) names.push_back({i, fd.name, fd.loc});
        for (std::uint32_t s = 0; s < f_.services.size(); ++s) {
            names.push_back({file_scope, f_.services[s].name, f_.services[s].loc});
            for (const auto& r : f_.services[s].rpcs) names.push_back({file_scope + 1 + s, r.name, r.loc});
        }
        std::sort(names.begin(), names.end(), [](const scoped_name& a, const scoped_name& b) {
            if (a.scope != b.scope) return a.scope < b.scope;
            if (a.name != b.name) return a.name < b.name;
            return before(a.loc, b.loc);
        });
        for (std::size_t k = 1; k < names.size(); ++k)
            if (names[k].scope == names[k - 1].scope && names[k].name == names[k - 1].name)
                return fail(errc::duplicate_definition, names[k].loc, names[k].name);
        return true;
    }

    constexpr void qualify() {
        if (f_.package.empty()) return;
        const std::string prefix = f_.package + ".";
        for (auto& m : f_.messages) m.full_name.insert(0, prefix);
        for (auto& e : f_.enums) e.full_name.insert(0, prefix);
        for (auto& s : f_.services) s.full_name.insert(0, prefix);
    }

    constexpr void build_names() {
        names_.reserve(f_.messages.size() + f_.enums.size() + f_.services.size() + 4);
        for (std::size_t i = 0; i < f_.messages.size(); ++i)
            names_.push_back({f_.messages[i].full_name, symbol::message, static_cast<std::uint32_t>(i)});
        for (std::size_t i = 0; i < f_.enums.size(); ++i)
            names_.push_back({f_.enums[i].full_name, symbol::enumeration, static_cast<std::uint32_t>(i)});
        for (std::size_t i = 0; i < f_.services.size(); ++i)
            names_.push_back({f_.services[i].full_name, symbol::service, static_cast<std::uint32_t>(i)});
        const std::string_view pkg = f_.package;
        for (std::size_t k = 0; k < pkg.size(); ++k)
            if (pkg[k] == '.') names_.push_back({pkg.substr(0, k), symbol::package, npos});
        if (!pkg.empty()) names_.push_back({pkg, symbol::package, npos});
        std::sort(names_.begin(), names_.end(),
                  [](const name_entry& a, const name_entry& b) { return a.full < b.full; });
    }

    [[nodiscard]] constexpr const name_entry* lookup(std::string_view full) const noexcept {
        const auto it = std::lower_bound(names_.begin(), names_.end(), full,
                                         [](const name_entry& e, std::string_view k) { return e.full < k; });
        if (it == names_.end() || it->full != full) return nullptr;
        return &*it;
    }

    // protoc's rule. A name with a leading dot is fully qualified. Otherwise its first component
    // is searched from the innermost scope outward, skipping symbols of the wrong kind (a
    // non-type for a simple name). For a dotted name the first symbol found decides: the rest
    // must exist inside it.
    [[nodiscard]] constexpr const name_entry* resolve_name(std::string_view scope, std::string_view name) const {
        if (!name.empty() && name.front() == '.') {
            const name_entry* e = lookup(name.substr(1));
            return e != nullptr && is_type(e->kind) ? e : nullptr;
        }
        std::size_t dot = name.size();
        for (std::size_t k = 0; k < name.size(); ++k) {
            if (name[k] == '.') {
                dot = k;
                break;
            }
        }
        const std::string_view first = name.substr(0, dot);
        std::string candidate;
        for (;;) {
            candidate.clear();
            if (!scope.empty()) {
                candidate.append(scope);
                candidate.push_back('.');
            }
            candidate.append(first);
            if (const name_entry* e = lookup(candidate); e != nullptr) {
                if (dot == name.size()) {
                    if (is_type(e->kind)) return e;
                } else {
                    candidate.append(name.substr(dot));
                    const name_entry* inner = lookup(candidate);
                    return inner != nullptr && is_type(inner->kind) ? inner : nullptr;
                }
            }
            if (scope.empty()) return nullptr;
            const auto cut = scope.rfind('.');
            scope = cut == std::string_view::npos ? std::string_view{} : scope.substr(0, cut);
        }
    }

    [[nodiscard]] constexpr bool resolve() {
        for (auto& m : f_.messages) {
            const std::string_view scope = m.full_name;
            for (auto& fd : m.fields) {
                if (fd.type_name.empty()) continue;
                const name_entry* e = resolve_name(scope, fd.type_name);
                if (e == nullptr) return fail(errc::unknown_type, fd.type_loc, fd.type_text);
                fd.kind = e->kind == symbol::message ? field_kind::message : field_kind::enumeration;
                fd.type_index = e->index;
            }
        }
        for (auto& s : f_.services) {
            for (auto& r : s.rpcs) {
                const name_entry* q = resolve_name(f_.package, r.request_name);
                if (q == nullptr) return fail(errc::unknown_type, r.request_loc, r.name);
                if (q->kind != symbol::message) return fail(errc::not_a_message_type, r.request_loc, r.name);
                const name_entry* p = resolve_name(f_.package, r.response_name);
                if (p == nullptr) return fail(errc::unknown_type, r.response_loc, r.name);
                if (p->kind != symbol::message) return fail(errc::not_a_message_type, r.response_loc, r.name);
                r.request = q->index;
                r.response = p->index;
            }
        }
        return true;
    }

    // An explicit [packed] is only valid on a repeated numeric, bool or enum field.
    [[nodiscard]] constexpr bool check_packed() {
        for (auto& m : f_.messages) {
            for (auto& fd : m.fields) {
                const bool packable =
                    fd.kind == field_kind::enumeration || (fd.kind == field_kind::scalar && is_packable(fd.type));
                if (fd.packed_option != -1 && !(fd.repeated && packable))
                    return fail(errc::packed_on_non_packable, fd.loc, fd.name);
                fd.packed = fd.repeated && packable && fd.packed_option != 0;
            }
        }
        return true;
    }

    // Depth-first post-order over message-typed fields: dependencies come first. A field that
    // reaches a message still on the stack closes a cycle.
    [[nodiscard]] constexpr bool order_messages() {
        const std::size_t n = f_.messages.size();
        std::vector<std::uint8_t> state(n, 0);  // 0 new, 1 on stack, 2 done
        struct frame {
            std::uint32_t msg;
            std::uint32_t next_field;
        };
        std::vector<frame> stack;
        f_.definition_order.reserve(n);
        for (std::uint32_t root = 0; root < n; ++root) {
            if (state[root] != 0) continue;
            stack.push_back({root, 0});
            state[root] = 1;
            while (!stack.empty()) {
                frame& top = stack.back();
                const message& m = f_.messages[top.msg];
                if (top.next_field == m.fields.size()) {
                    state[top.msg] = 2;
                    f_.definition_order.push_back(top.msg);
                    stack.pop_back();
                    continue;
                }
                const field& fd = m.fields[top.next_field++];
                if (fd.kind != field_kind::message) continue;
                if (state[fd.type_index] == 1) return fail(errc::recursive_message, fd.type_loc, fd.type_text);
                if (state[fd.type_index] == 0) {
                    state[fd.type_index] = 1;
                    stack.push_back({fd.type_index, 0});
                }
            }
        }
        return true;
    }
};

}  // namespace detail

// Parses and checks a proto3 schema. On success every type reference is resolved and
// `definition_order` lists messages with their dependencies first.
[[nodiscard]] constexpr std::expected<file, error> parse(std::string_view source) {
    auto toks = tokenize(source);
    if (!toks) return std::unexpected(toks.error());
    detail::parser p(std::move(*toks));
    if (!p.parse_file()) return std::unexpected(p.failure());
    file f = std::move(p.result());
    detail::checker c(f);
    if (!c.run()) return std::unexpected(c.failure());
    return f;
}

}  // namespace tep::schema
