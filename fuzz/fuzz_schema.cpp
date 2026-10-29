// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Fuzz target: the schema front end (lexer, parser, checks) on arbitrary input, at run time. The
// same code runs in constant evaluation, where the compiler itself rejects any out-of-bounds
// access; this target covers the run-time path under the sanitizers. Any input gives either an
// error with a position, or a file whose cross references hold.
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

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace tep::schema;
    const std::string_view src(reinterpret_cast<const char*>(data), size);
    const auto f = parse(src);
    if (!f) {
        require(f.error().loc.line >= 1 && f.error().loc.column >= 1);
        return 0;
    }
    const std::size_t nm = f->messages.size();
    for (const message& m : f->messages) {
        require(m.parent == npos || m.parent < nm);
        require(!m.full_name.empty());
        for (const field& fd : m.fields) {
            if (fd.kind == field_kind::message) require(fd.type_index < nm);
            if (fd.kind == field_kind::enumeration) require(fd.type_index < f->enums.size());
        }
    }
    for (const service& s : f->services)
        for (const rpc& r : s.rpcs) require(r.request < nm && r.response < nm);
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
