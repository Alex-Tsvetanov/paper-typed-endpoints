// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Writes the load generators' request bodies: each benchmark request message behind its 5-byte
// gRPC prefix, the bytes h2load posts with --data, and the message alone, the bytes ghz sends with
// --binary-file. Each file is read back and must frame (the .bin) and parse.
//
// Usage: write_bodies <folder>   writes small.bin and small.pb (a Person), medium.bin and medium.pb
// (an Order), and smaller.bin and smaller.pb: the medium Order without its last item and its
// customer, the smaller order test_allocs alternates with (the mixed-traffic windows,
// hypotheses-r2.md section 4)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>

#include "e2e.pb.h"
#include "requests.hpp"
#include "tep/grpc.hpp"

namespace {

// Writes bytes to a file and returns what reading it back gives, empty if writing failed.
std::string write_and_read(const std::filesystem::path& file, const std::string& bytes) {
    {
        std::ofstream out(file, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) return {};
    }
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// <stem>.bin holds the framed message, <stem>.pb the message alone.
template <class M>
bool write_body(const std::filesystem::path& folder, const char* stem, const std::string& message) {
    std::string body(tep::grpc::prefix_size, '\0');
    tep::grpc::write_prefix(reinterpret_cast<std::byte*>(body.data()), false,
                            static_cast<std::uint32_t>(message.size()));
    body += message;
    const std::string back = write_and_read(folder / (std::string(stem) + ".bin"), body);
    const auto frame =
        tep::grpc::parse_unary(std::span<const std::byte>(reinterpret_cast<const std::byte*>(back.data()), back.size()));
    M parsed;
    const bool framed = back == body && frame && !frame->compressed &&
                        parsed.ParseFromArray(frame->payload.data(), static_cast<int>(frame->payload.size()));
    const std::string bare = write_and_read(folder / (std::string(stem) + ".pb"), message);
    M parsed_bare;
    return framed && bare == message && parsed_bare.ParseFromString(bare) &&
           parsed_bare.SerializeAsString() == parsed.SerializeAsString();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: write_bodies <folder>\n");
        return 2;
    }
    const std::filesystem::path folder = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        std::fprintf(stderr, "write_bodies: cannot create %s\n", folder.string().c_str());
        return 1;
    }
    const bool small = write_body<bench::e2e::Person>(folder, "small", e2e::small_request());
    const bool medium = write_body<bench::e2e::Order>(folder, "medium", e2e::medium_request());
    // As test_allocs builds its smaller order: the medium request less its last item and customer.
    bench::e2e::Order shorter;
    bool smaller = shorter.ParseFromString(e2e::medium_request()) && shorter.items_size() > 1 && shorter.has_customer();
    if (smaller) {
        shorter.mutable_items()->RemoveLast();
        shorter.clear_customer();
        smaller = write_body<bench::e2e::Order>(folder, "smaller", shorter.SerializeAsString());
    }
    std::printf("write_bodies: small %s, medium %s, smaller %s\n", small ? "ok" : "FAILED", medium ? "ok" : "FAILED",
                smaller ? "ok" : "FAILED");
    return small && medium && smaller ? 0 : 1;
}
