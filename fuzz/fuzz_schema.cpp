// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the schema front end (lexer, parser, checks) on arbitrary input, at run time. The
// same code runs in constant evaluation, where the compiler itself rejects any out-of-bounds
// access; this target covers the run-time path under the sanitizers. Any input gives either an
// error at a position inside the source, or a file whose names and cross references hold. Every
// name is read byte by byte, so a view that dangles shows up under ASan. Run libFuzzer with
// -max_len=65536 -rss_limit_mb=512 so that inputs with many definitions are tried too.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "tep/schema.hpp"

namespace {

void require(bool ok) {
    if (!ok) std::abort();
}

volatile std::uint32_t sink = 0;

// Reads every byte, so ASan sees a view that points at freed or foreign memory.
void touch(std::string_view s) {
    std::uint32_t h = 0;
    for (const char c : s) h = h * 31u + static_cast<unsigned char>(c);
    sink = sink + h;
}

// The full name is the scope's full name, a dot and the name (the name alone at the top of a file
// without a package).
bool joined(std::string_view full, std::string_view scope, std::string_view name) {
    if (scope.empty()) return full == name;
    return full.size() == scope.size() + 1 + name.size() && full.substr(0, scope.size()) == scope &&
           full[scope.size()] == '.' && full.substr(scope.size() + 1) == name;
}

void check_error(std::string_view src, const tep::schema::error& e) {
    std::size_t lines = 1;
    for (const char c : src) lines += c == '\n' ? 1u : 0u;
    require(e.loc.line >= 1 && e.loc.line <= lines && e.loc.column >= 1);
    // The column lies within its line, or one past its end.
    std::size_t start = 0;
    for (std::uint32_t l = 1; l < e.loc.line; ++l) start = src.find('\n', start) + 1;
    const std::size_t stop = src.find('\n', start);
    const std::size_t length = (stop == std::string_view::npos ? src.size() : stop) - start;
    require(e.loc.column <= length + 1);
    // An error token that points into the source ends inside it too.
    const char* t = e.token.data();
    if (!e.token.empty() && t >= src.data() && t < src.data() + src.size())
        require(t + e.token.size() <= src.data() + src.size());
    touch(e.token);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace tep::schema;
    const std::string_view src(reinterpret_cast<const char*>(data), size);
    const auto f = parse(src);
    if (!f) {
        check_error(src, f.error());
        return 0;
    }
    const std::string_view pkg = f->package;
    const std::size_t nm = f->messages.size();
    for (std::size_t i = 0; i < nm; ++i) {
        const message& m = f->messages[i];
        touch(m.name);
        touch(m.full_name);
        require(m.parent == npos || m.parent < i);  // parents come before their children
        require(joined(m.full_name, m.parent == npos ? pkg : std::string_view(f->messages[m.parent].full_name), m.name));
        for (const field& fd : m.fields) {
            touch(fd.name);
            touch(fd.type_name);
            touch(fd.type_text);
            touch(fd.number_text);
            if (fd.kind == field_kind::scalar) require(fd.type_index == npos && fd.type_name.empty());
            if (fd.kind == field_kind::message) require(fd.type_index < nm);
            if (fd.kind == field_kind::enumeration) require(fd.type_index < f->enums.size());
            require(!fd.packed || fd.repeated);
        }
    }
    for (const enumeration& e : f->enums) {
        touch(e.full_name);
        require(e.parent == npos || e.parent < nm);
        require(joined(e.full_name, e.parent == npos ? pkg : std::string_view(f->messages[e.parent].full_name), e.name));
        for (const enum_value& v : e.values) touch(v.name);
    }
    for (const service& s : f->services) {
        touch(s.full_name);
        require(joined(s.full_name, pkg, s.name));
        for (const rpc& r : s.rpcs) {
            touch(r.name);
            touch(r.request_name);
            touch(r.response_name);
            require(r.request < nm && r.response < nm);
        }
    }
    // The definition order is a permutation of the messages, dependencies first.
    require(f->definition_order.size() == nm);
    std::vector<std::uint8_t> done(nm, 0);
    for (const std::uint32_t i : f->definition_order) {
        require(i < nm && done[i] == 0);
        for (const field& fd : f->messages[i].fields)
            if (fd.kind == field_kind::message) require(done[fd.type_index] == 1);
        done[i] = 1;
    }
    return 0;
}
