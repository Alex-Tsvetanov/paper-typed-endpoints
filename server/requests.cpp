// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
#include "requests.hpp"

#include <string>

#include "e2e.pb.h"

std::string e2e::small_request() {
    bench::e2e::Person p;
    p.set_name("Ada Lovelace");
    p.set_id(1815);
    p.set_email("ada@example.org");
    return p.SerializeAsString();
}

std::string e2e::medium_request() {
    bench::e2e::Order o;
    o.set_order_id("order-0000000042");
    o.set_created_unix_ms(1793300000000);
    bench::e2e::Person* c = o.mutable_customer();
    c->set_name("Grace Hopper");
    c->set_id(1906);
    c->set_email("grace@example.org");
    for (int i = 0; i < 12; ++i) {
        bench::e2e::Item* it = o.add_items();
        it->set_id(100000 + i * 7919);
        it->set_name("item-" + std::to_string(i) + "-standard-widget");
        it->set_price(9.99 + i * 1.25);
        for (int t = 0; t < 4; ++t) it->add_tags(i * 10 + t);
        it->set_available(i % 3 != 0);
    }
    for (const char* label : {"express", "gift-wrap", "fragile", "priority-customer", "eu-warehouse"})
        o.add_labels(label);
    o.set_note(std::string(256, 'n'));
    o.set_priority(-3);
    o.set_checksum(0x0123'4567'89AB'CDEFull);
    return o.SerializeAsString();
}
