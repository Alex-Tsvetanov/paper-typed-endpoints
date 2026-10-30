// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The end-to-end benchmark's arms behind one interface (design note section 3): the same request
// bytes in, the framed response written into the same buffer type, so the arms differ only in
// what their generators produce (decoding, dispatch, encoding). Both serve the service of e2e.proto.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "tep/grpc.hpp"

namespace e2e {

class arm {
public:
    arm() = default;
    arm(const arm&) = delete;
    arm& operator=(const arm&) = delete;
    virtual ~arm() = default;

    // One unary call. `request` is the gRPC message without its 5-byte prefix. On ok, `response`
    // holds the length-prefixed response message; otherwise it is empty.
    virtual tep::grpc::status call(std::string_view path, std::span<const std::byte> request,
                                   std::vector<std::byte>& response) = 0;
};

// R: messages and endpoint synthesized from e2e.proto by C++26 reflection (tep).
std::unique_ptr<arm> make_reflection_arm();

// P: protoc-generated classes and libprotobuf, with a route table of the same shape.
std::unique_ptr<arm> make_protoc_arm();

// S: no codec; the response is the request's bytes (arm_s.cpp). The transport's test arm and its
// lower bound.
std::unique_ptr<arm> make_stub_arm();

}  // namespace e2e
