// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Heap allocations per call of each arm, counted by replacing the global operator new. A count,
// not a timing: deterministic for a given build, so it can explain a difference in throughput
// without timing anything here. The response buffer is reused across calls, as the transport
// reuses it, so its own growth is not counted after the first call. Arm R's counts are bounded,
// so a regression fails; arm Q's must stay 0 (reuse working); arm P's are only printed. Skipped
// under GCC's ASan and TSan, whose runtimes define the array and nothrow forms themselves.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "arm.hpp"
#include "check.hpp"
#include "requests.hpp"

namespace {

std::size_t allocations = 0;
std::size_t allocated_bytes = 0;

}  // namespace

// The library's array and nothrow forms call this one (without a sanitizer runtime). The
// over-aligned forms are left alone: no type of any arm is over-aligned.
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

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool sanitized = true;
#else
constexpr bool sanitized = false;
#endif

// Prints and returns the allocations per call.
double count(const char* arm_name, e2e::arm& a, const char* payload, const char* path, const std::string& message) {
    const std::span<const std::byte> request{reinterpret_cast<const std::byte*>(message.data()), message.size()};
    std::vector<std::byte> response;
    TEP_CHECK(a.call(path, request, response) == tep::grpc::status::ok);  // warm: sizes the response
    const std::size_t n0 = allocations;
    const std::size_t b0 = allocated_bytes;
    for (int i = 0; i < calls; ++i) TEP_CHECK(a.call(path, request, response) == tep::grpc::status::ok);
    const double per_call = static_cast<double>(allocations - n0) / calls;
    std::printf("arm %s %s: %.2f allocations, %.1f bytes per call\n", arm_name, payload, per_call,
                static_cast<double>(allocated_bytes - b0) / calls);
    return per_call;
}

}  // namespace

int main() {
    if (sanitized) {
        std::printf("SKIPPED: a sanitizer runtime defines operator new's other forms; counts need a plain build\n");
        return 77;
    }
    const std::string small = e2e::small_request();
    const std::string medium = e2e::medium_request();
    const auto s = e2e::make_stub_arm();
    const auto r = e2e::make_reflection_arm();
    const auto p = e2e::make_protoc_arm();
    const auto q = e2e::make_protoc_reuse_arm();
    for (const auto& [name, a] :
         {std::pair{"S", s.get()}, std::pair{"R", r.get()}, std::pair{"P", p.get()}, std::pair{"Q", q.get()}}) {
        const double at_small = count(name, *a, "small", "/bench.e2e.Echo/EchoPerson", small);
        const double at_medium = count(name, *a, "medium", "/bench.e2e.Echo/EchoOrder", medium);
        if (std::string_view(name) == "S") TEP_CHECK(at_small == 0 && at_medium == 0);
        // R: nothing for Person (short strings). For Order, the request is parsed into the storage of
        // the previous one (no allocation); the copy into the cleared response allocates its nested
        // customer and its email, 12 items (each a name and its tags) and one long label.
        if (std::string_view(name) == "R") TEP_CHECK(at_small == 0 && at_medium <= 27);
        // Q: protobuf's Clear() keeps repeated elements, string capacity and the nested customer
        // (protobuf 36.2, pinned), so a steady run of identical requests allocates nothing.
        if (std::string_view(name) == "Q") TEP_CHECK(at_small == 0 && at_medium == 0);
    }
    return tep_test::finish("test_allocs");
}
