// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Writes the load generator's request bodies: each benchmark request message behind its 5-byte
// gRPC prefix, the bytes h2load posts with --data. Each file is read back and must frame and parse.
//
// Usage: write_bodies <folder>   writes small.bin (a Person) and medium.bin (an Order)
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

template <class M>
bool write_body(const std::filesystem::path& file, const std::string& message) {
    std::string body(tep::grpc::prefix_size, '\0');
    tep::grpc::write_prefix(reinterpret_cast<std::byte*>(body.data()), false,
                            static_cast<std::uint32_t>(message.size()));
    body += message;
    {
        std::ofstream out(file, std::ios::binary);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) return false;
    }
    std::ifstream in(file, std::ios::binary);
    const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto frame =
        tep::grpc::parse_unary(std::span<const std::byte>(reinterpret_cast<const std::byte*>(back.data()), back.size()));
    M parsed;
    return back == body && frame && !frame->compressed &&
           parsed.ParseFromArray(frame->payload.data(), static_cast<int>(frame->payload.size()));
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
    const bool small = write_body<bench::e2e::Person>(folder / "small.bin", e2e::small_request());
    const bool medium = write_body<bench::e2e::Order>(folder / "medium.bin", e2e::medium_request());
    std::printf("write_bodies: small.bin %s, medium.bin %s\n", small ? "ok" : "FAILED", medium ? "ok" : "FAILED");
    return small && medium ? 0 : 1;
}
