// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>

namespace tep {

// A string literal usable as a template argument (for names such as "tutorial.Person").
template <std::size_t N>
struct fixed_string {
    char data[N]{};

    consteval fixed_string(const char (&s)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {data, N - 1}; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return N - 1; }
};

}  // namespace tep
