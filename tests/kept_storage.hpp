// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// Storage a reused message keeps for later calls (tep::repeated's elements past size(), the
// storage of an absent tep::box) must hold no data from earlier messages: every such element and
// message equals its default, at every depth. Checked by walking the message by reflection.
#pragma once

#include <cstddef>
#include <meta>

#include "tep/reflect.hpp"

namespace tep_test {

template <class T>
bool kept_is_default(const T& v);

template <class M>
bool kept_is_default(const tep::box<M>& b) {
    const M* const p = b.kept_storage();
    if (p == nullptr) return true;
    if (!b.has_value() && !(*p == M{})) return false;
    return kept_is_default(*p);
}

template <class E>
bool kept_is_default(const tep::repeated<E>& r) {
    const auto all = r.kept_storage();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (i >= r.size() && !(all[i] == E{})) return false;
        if (!kept_is_default(all[i])) return false;
    }
    return true;
}

template <class T>
bool kept_is_default(const T& v) {
    if constexpr (tep::synthesized_message<T>) {
        bool ok = true;
        template for (constexpr auto mem : std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) {
            ok = ok && kept_is_default(v.[:mem:]);
        }
        return ok;
    } else {
        return true;
    }
}

}  // namespace tep_test
