// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// How repeated fields grow while decoding, counted by replacing the global operator new. Elements
// of a repeated field that arrive together take one allocation; elements that arrive in many
// pieces (packed chunks, or interleaved with another field) grow the vector geometrically, so
// decoding stays linear in the input. Only vectors of trivially copyable elements and short
// strings appear, so every allocation counted is a vector's.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "tep/reflect.hpp"

namespace {

std::size_t allocations = 0;

}  // namespace

void* operator new(std::size_t n) {
    ++allocations;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct growth_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.growth;
message Leaf { int32 v = 1; }
message Bag {
  repeated fixed32 f = 1;
  repeated int32 v = 2;
  repeated Leaf items = 3;
  repeated string s = 4;
}
)";
};

consteval { tep::define_messages<growth_proto>(); }

using Bag = tep::message_t<growth_proto, "tep.growth.Bag">;

namespace {

void put(std::vector<std::byte>& out, std::initializer_list<int> bytes) {
    for (const int b : bytes) out.push_back(static_cast<std::byte>(b));
}

// One field element per call; tags are field number << 3 | 2 (length-delimited).
void fixed_chunk(std::vector<std::byte>& out, int i) { put(out, {0x0A, 4, i & 0xFF, (i >> 8) & 0xFF, 0, 0}); }
void varint_chunk(std::vector<std::byte>& out, int i) { put(out, {0x12, 1, i & 0x7F}); }
void item(std::vector<std::byte>& out, int i) { put(out, {0x1A, 2, 0x08, i & 0x7F}); }
void short_string(std::vector<std::byte>& out) { put(out, {0x22, 1, 'a'}); }

// Decodes `in` and returns the number of allocations it took; the result is checked by `check`.
template <class Check>
std::size_t decode_counting(const std::vector<std::byte>& in, Check check) {
    const std::size_t before = allocations;
    const auto bag = tep::decode<Bag>(in);
    const std::size_t n = allocations - before;
    TEP_CHECK(bag.has_value());
    if (bag) check(*bag);
    return n;
}

void test_together_is_one_allocation() {
    std::vector<std::byte> run;
    put(run, {0x12, 64});
    for (int i = 0; i < 64; ++i) put(run, {i});
    TEP_CHECK(decode_counting(run, [](const Bag& b) { TEP_CHECK(b.v.size() == 64 && b.v[63] == 63); }) == 1);

    std::vector<std::byte> items;
    for (int i = 0; i < 12; ++i) item(items, i);
    TEP_CHECK(decode_counting(items, [](const Bag& b) { TEP_CHECK(b.items.size() == 12 && b.items[11].v == 11); }) ==
              1);

    std::vector<std::byte> strings;
    for (int i = 0; i < 5; ++i) short_string(strings);
    TEP_CHECK(decode_counting(strings, [](const Bag& b) { TEP_CHECK(b.s.size() == 5 && b.s[4] == "a"); }) == 1);
}

// 1000 elements in pieces: geometric growth takes about log2(1000) = 10 allocations per vector,
// an exact reservation per piece would take 1000.
constexpr int pieces = 1000;
constexpr std::size_t geometric_bound = 16;

void test_pieces_grow_geometrically() {
    std::vector<std::byte> fixed;
    for (int i = 0; i < pieces; ++i) fixed_chunk(fixed, i);
    TEP_CHECK(decode_counting(fixed, [](const Bag& b) { TEP_CHECK(b.f.size() == pieces && b.f[999] == 999); }) <=
              geometric_bound);

    std::vector<std::byte> varints;
    for (int i = 0; i < pieces; ++i) varint_chunk(varints, i);
    TEP_CHECK(decode_counting(varints, [](const Bag& b) { TEP_CHECK(b.v.size() == pieces && b.v[999] == 999 % 128); }) <=
              geometric_bound);

    std::vector<std::byte> mixed;
    for (int i = 0; i < pieces; ++i) {
        item(mixed, i);
        short_string(mixed);
    }
    TEP_CHECK(decode_counting(mixed, [](const Bag& b) {
                  TEP_CHECK(b.items.size() == pieces && b.s.size() == pieces && b.items[999].v == 999 % 128);
              }) <= 2 * geometric_bound);
}

}  // namespace

int main() {
    test_together_is_one_allocation();
    test_pieces_grow_geometrically();
    return tep_test::finish("test_growth");
}
