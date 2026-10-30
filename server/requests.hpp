// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The benchmark's request messages, built with the protoc-generated classes. Their bytes feed both
// arms and the load generator's request bodies.
#pragma once

#include <string>

namespace e2e {

std::string small_request();   // a Person
std::string medium_request();  // an Order of about 1 KiB

}  // namespace e2e
