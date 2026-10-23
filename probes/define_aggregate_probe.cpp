// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Laptop request 1 (STATE.md): does this g++ accept C++26 reflection's define_aggregate?
//   g++ -std=c++26 -freflection probes/define_aggregate_probe.cpp -o /tmp/define_aggregate_probe
//   /tmp/define_aggregate_probe
// Expected: it compiles without diagnostics, prints
//   define_aggregate: ok, __cpp_impl_reflection 202603
// and exits 0. A compile error means this compiler cannot build P5's reflection layer.
#include <meta>

#include <cstdio>
#include <string>
#include <vector>

#ifndef __cpp_impl_reflection
#error "no __cpp_impl_reflection: this compiler or these flags lack C++26 reflection"
#endif

struct probe;

consteval {
    std::meta::define_aggregate(^^probe, std::vector{
                                             std::meta::data_member_spec(^^int, {.name = "id"}),
                                             std::meta::data_member_spec(^^std::string, {.name = "name"}),
                                         });
}

static_assert(std::meta::nonstatic_data_members_of(^^probe, std::meta::access_context::unchecked()).size() == 2);

int main() {
    probe p{7, "seven"};
    if (p.id != 7 || p.name != "seven") return 1;
    std::printf("define_aggregate: ok, __cpp_impl_reflection %ld\n", static_cast<long>(__cpp_impl_reflection));
    return 0;
}
