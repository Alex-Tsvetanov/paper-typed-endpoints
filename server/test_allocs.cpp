// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Heap allocations per call of each arm, counted by replacing the global operator new. A count,
// not a timing: deterministic for a given build, so it can explain a difference in throughput
// without timing anything here. The response buffer is reused across calls, as the transport
// reuses it, so its own growth is not counted after the first call.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "arm.hpp"
#include "check.hpp"
#include "requests.hpp"

namespace {

std::size_t allocations = 0;
std::size_t allocated_bytes = 0;

}  // namespace

// The library's array and nothrow forms call this one. The over-aligned forms are left alone: no
// type of either arm is over-aligned.
void* operator new(std::size_t n) {
    ++allocations;
    allocated_bytes += n;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr int calls = 100;

void count(const char* arm_name, e2e::arm& a, const char* payload, const char* path, const std::string& message) {
    const std::span<const std::byte> request{reinterpret_cast<const std::byte*>(message.data()), message.size()};
    std::vector<std::byte> response;
    TEP_CHECK(a.call(path, request, response) == tep::grpc::status::ok);  // warm: sizes the response
    const std::size_t n0 = allocations;
    const std::size_t b0 = allocated_bytes;
    for (int i = 0; i < calls; ++i) TEP_CHECK(a.call(path, request, response) == tep::grpc::status::ok);
    std::printf("arm %s %s: %.2f allocations, %.1f bytes per call\n", arm_name, payload,
                static_cast<double>(allocations - n0) / calls, static_cast<double>(allocated_bytes - b0) / calls);
}

}  // namespace

int main() {
    const std::string small = e2e::small_request();
    const std::string medium = e2e::medium_request();
    const auto s = e2e::make_stub_arm();
    const auto r = e2e::make_reflection_arm();
    const auto p = e2e::make_protoc_arm();
    for (const auto& [name, a] : {std::pair{"S", s.get()}, std::pair{"R", r.get()}, std::pair{"P", p.get()}}) {
        count(name, *a, "small", "/bench.e2e.Echo/EchoPerson", small);
        count(name, *a, "medium", "/bench.e2e.Echo/EchoOrder", medium);
    }
    return tep_test::finish("test_allocs");
}
