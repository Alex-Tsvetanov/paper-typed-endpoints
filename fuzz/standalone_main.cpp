// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// A driver for the fuzz targets where libFuzzer is not available (GCC, MSVC): it runs every seed
// file, then a fixed number of deterministic mutations of them. A smoke test of the targets
// under each preset's sanitizers, not a substitute for libFuzzer's coverage guidance.
//
// Usage: <target> <mutations> <seed file>...
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

using bytes = std::vector<std::uint8_t>;

struct xorshift {
    std::uint64_t s;
    std::uint64_t next() noexcept {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    std::size_t below(std::size_t n) noexcept { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
    std::uint8_t byte() noexcept { return static_cast<std::uint8_t>(next() & 0xFFu); }
};

std::ptrdiff_t at(std::size_t i) { return static_cast<std::ptrdiff_t>(i); }

// One to four edits: flip a bit, replace, insert or delete a byte, truncate, or splice in a run of
// another seed.
bytes mutate(bytes v, const std::vector<bytes>& seeds, xorshift& r) {
    const std::size_t edits = 1 + r.below(4);
    for (std::size_t e = 0; e < edits; ++e) {
        switch (r.below(6)) {
            case 0:
                if (!v.empty()) v[r.below(v.size())] ^= static_cast<std::uint8_t>(1u << r.below(8));
                break;
            case 1:
                if (!v.empty()) v[r.below(v.size())] = r.byte();
                break;
            case 2: v.insert(v.begin() + at(r.below(v.size() + 1)), r.byte()); break;
            case 3:
                if (!v.empty()) v.erase(v.begin() + at(r.below(v.size())));
                break;
            case 4:
                if (!v.empty()) v.resize(r.below(v.size()));
                break;
            default: {
                const bytes& o = seeds[r.below(seeds.size())];
                if (o.empty()) break;
                const std::size_t from = r.below(o.size());
                const std::size_t n = 1 + r.below(o.size() - from);
                v.insert(v.begin() + at(r.below(v.size() + 1)), o.begin() + at(from), o.begin() + at(from + n));
                break;
            }
        }
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <mutations> <seed file>...\n", argv[0]);
        return 2;
    }
    const long mutations = std::strtol(argv[1], nullptr, 10);
    std::vector<bytes> seeds;
    for (int i = 2; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            return 2;
        }
        const std::string s{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        bytes b(s.size());
        for (std::size_t k = 0; k < s.size(); ++k) b[k] = static_cast<std::uint8_t>(s[k]);
        seeds.push_back(std::move(b));
    }
    for (const bytes& s : seeds) LLVMFuzzerTestOneInput(s.data(), s.size());
    xorshift r{0x9E3779B97F4A7C15u};
    bytes current;
    for (long i = 0; i < mutations; ++i) {
        // Walk on from the last input, and restart from a seed every fourth step.
        current = mutate(i % 4 == 0 || current.empty() ? seeds[r.below(seeds.size())] : current, seeds, r);
        LLVMFuzzerTestOneInput(current.data(), current.size());
    }
    std::printf("%zu seeds and %ld mutations, no failure\n", seeds.size(), mutations);
    return 0;
}
