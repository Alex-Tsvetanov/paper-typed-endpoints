// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// A minimal always-on check: it does not depend on NDEBUG, so a Release build cannot skip a
// check that has side effects.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <source_location>
#include <string>
#include <vector>

namespace tep_test {

inline std::atomic<int> failures{0};  // checks may run on several threads (test_server's clients)

inline void check(bool ok, const char* expr, std::source_location loc = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "%s:%u: check failed: %s\n", loc.file_name(), static_cast<unsigned>(loc.line()), expr);
    }
}

inline int finish(const char* name) {
    if (failures == 0) {
        std::printf("%s: all checks passed\n", name);
        return 0;
    }
    std::fprintf(stderr, "%s: %d checks failed\n", name, failures.load());
    return 1;
}

inline std::vector<std::byte> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        ++failures;
        return {};
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) out[i] = static_cast<std::byte>(raw[i]);
    return out;
}

inline std::vector<std::byte> bytes(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    out.reserve(v.size());
    for (const int b : v) out.push_back(static_cast<std::byte>(b));
    return out;
}

}  // namespace tep_test

#define TEP_CHECK(...) ::tep_test::check(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__)
