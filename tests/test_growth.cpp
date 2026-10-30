// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// How repeated fields grow while decoding, counted by replacing the global operator new. Elements
// of a repeated field that arrive together take one allocation; elements that arrive in many
// pieces (packed chunks, or interleaved with another field) grow the vector geometrically, so
// decoding stays linear in the input; and a reservation made by looking ahead is capped, so a run
// whose first element fails reserves little. Only vectors of trivially copyable elements and
// short strings appear, so every allocation counted is a vector's.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "kept_storage.hpp"
#include "tep/reflect.hpp"

namespace {

std::size_t allocations = 0;
std::size_t allocated_bytes = 0;
std::size_t throw_at = 0;  // when nonzero, the allocation with this ordinal throws std::bad_alloc

}  // namespace

// Out of line, so the optimizer never pairs an inlined malloc with a std::allocator deallocation
// (-Wmismatched-new-delete).
[[gnu::noinline]] void* operator new(std::size_t n) {
    ++allocations;
    if (throw_at != 0 && allocations == throw_at) throw std::bad_alloc();
    allocated_bytes += n;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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
void varint(std::vector<std::byte>& out, std::size_t v) {
    for (; v >= 0x80; v >>= 7) out.push_back(static_cast<std::byte>((v & 0x7F) | 0x80));
    out.push_back(static_cast<std::byte>(v));
}

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
// an exact reservation per piece would take 1000. Only the fixed-width chunks failed this before
// the reservations were made geometric; the other two cases guard against an exact reservation
// being added later.
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

// Decodes `in`, which must fail with `want`, and returns the bytes allocated meanwhile.
std::size_t failing_bytes(const std::vector<std::byte>& in, tep::decode_error want) {
    const std::size_t before = allocated_bytes;
    const auto bag = tep::decode<Bag>(in);
    const std::size_t n = allocated_bytes - before;
    TEP_CHECK(!bag.has_value() && bag.error() == want);
    return n;
}

// Inputs that fail inside a run: the right error, and no reservation for the rest of the run
// beyond the cap (100,000 empty elements would take 400 KB of Leaf, 3.2 MB of strings).
void test_malformed_runs() {
    constexpr int run = 100000;
    constexpr std::size_t cap = 2 * tep::detail::max_hint_bytes;

    std::vector<std::byte> first_item_fails;
    put(first_item_fails, {0x1A, 2, 0x08, 0x80});  // a Leaf whose varint is cut off
    for (int i = 0; i < run; ++i) put(first_item_fails, {0x1A, 0});
    TEP_CHECK(failing_bytes(first_item_fails, tep::decode_error::truncated) <= cap);

    std::vector<std::byte> second_string_fails;
    short_string(second_string_fails);
    put(second_string_fails, {0x22, 1, 0xFF});  // not UTF-8
    for (int i = 0; i < run; ++i) put(second_string_fails, {0x22, 0});
    TEP_CHECK(failing_bytes(second_string_fails, tep::decode_error::invalid_utf8) <= cap);

    std::vector<std::byte> bad_packed_varint;
    put(bad_packed_varint, {0x12});
    varint(bad_packed_varint, 10 + run);
    for (int i = 0; i < 10; ++i) put(bad_packed_varint, {0xFF});  // eleven bytes long
    for (int i = 0; i < run; ++i) put(bad_packed_varint, {0});
    TEP_CHECK(failing_bytes(bad_packed_varint, tep::decode_error::malformed_varint) <= cap);

    // A six-byte tag of the same field inside a run: the look ahead counts it, decoding refuses it.
    std::vector<std::byte> long_tag;
    item(long_tag, 1);
    put(long_tag, {0x9A, 0x80, 0x80, 0x80, 0x80, 0x00, 2, 0x08, 2});
    failing_bytes(long_tag, tep::decode_error::bad_tag);

    std::vector<std::byte> last_item_cut;
    item(last_item_cut, 1);
    put(last_item_cut, {0x1A, 5, 0x08});
    failing_bytes(last_item_cut, tep::decode_error::truncated);
}

}  // namespace

// The endpoint's reused messages: a steady run of calls allocates nothing, and a call over the
// 64 KiB limit or a failed call releases them (the next call allocates them again).
struct slot_proto {
    static constexpr std::string_view text = R"(syntax = "proto3";
package tep.slot;
message Msg { int32 n = 1; string s = 2; repeated int32 v = 3; }
service Svc { rpc Echo(Msg) returns (Msg); }
)";
};

consteval { tep::define_messages<slot_proto>(); }

using Msg = tep::message_t<slot_proto, "tep.slot.Msg">;

struct slot_impl {
    tep::grpc::status Echo(const Msg& q, Msg& r) {
        if (q.n < 0) return tep::grpc::status::invalid_argument;
        r = q;
        return tep::grpc::status::ok;
    }
};

using slot_endpoint = tep::endpoint<slot_proto, "tep.slot.Svc", slot_impl>;

namespace {

void test_endpoint_slots() {
    slot_impl impl;
    slot_endpoint ep{impl};
    std::vector<std::byte> response;
    const Msg m{.n = 1, .s = "short", .v = {1, 2, 3}};
    Msg big = m;
    big.v.assign(40'000, 1000);  // two bytes each: over 64 KiB
    Msg bad = m;
    bad.n = -1;
    const auto small = tep::encode(m);
    const auto large = tep::encode(big);
    const auto failing = tep::encode(bad);
    const auto allocations_of = [&](const std::vector<std::byte>& request, tep::grpc::status want) {
        const std::size_t before = allocations;
        TEP_CHECK(ep.invoke("/tep.slot.Svc/Echo", request, response) == want);
        return allocations - before;
    };
    using enum tep::grpc::status;
    allocations_of(small, ok);  // makes the slot and sizes the response buffer
    TEP_CHECK(allocations_of(small, ok) == 0);
    allocations_of(large, ok);
    TEP_CHECK(allocations_of(small, ok) > 0);  // the large call released the slot
    TEP_CHECK(allocations_of(small, ok) == 0);
    allocations_of(failing, invalid_argument);
    TEP_CHECK(allocations_of(small, ok) > 0);  // so did the failed call
    TEP_CHECK(allocations_of(small, ok) == 0);
}

// A copy or a parse that runs out of memory midway leaves a valid message whose kept storage holds
// no data: each allocation the operation makes fails once, in turn.
void test_out_of_memory_keeps_storage_clean() {
    Bag rich{};
    rich.s = {std::string(40, 'a'), std::string(40, 'b'), std::string(40, 'c')};
    rich.items.resize(5);
    rich.v = {1, 2, 3};
    rich.f = {7, 8};
    const auto bytes = tep::encode(rich);
    std::size_t failures = 0;
    for (std::size_t k = 1; k <= 40; ++k) {
        Bag dst{};
        dst.s = {std::string(40, 'x'), std::string(40, 'y')};
        dst.s.clear();  // two kept strings with storage
        throw_at = allocations + k;
        try {
            dst = rich;
        } catch (const std::bad_alloc&) {
            ++failures;
        }
        throw_at = 0;
        TEP_CHECK(tep_test::kept_is_default(dst));
        Bag parsed = rich;
        throw_at = allocations + k;
        try {
            tep::decode_error err{};
            (void)tep::parse_from(parsed, tep::wire::reader(bytes), err);
        } catch (const std::bad_alloc&) {
            ++failures;
        }
        throw_at = 0;
        TEP_CHECK(tep_test::kept_is_default(parsed));
    }
    TEP_CHECK(failures > 0);  // some allocation inside the operations did fail
}

}  // namespace

int main() {
    test_endpoint_slots();
    test_out_of_memory_keeps_storage_clean();
    test_together_is_one_allocation();
    test_pieces_grow_geometrically();
    test_malformed_runs();
    return tep_test::finish("test_growth");
}
