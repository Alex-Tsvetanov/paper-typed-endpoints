// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0

// Package expect holds the messages of the interoperability check (design note section 6): the
// same values as the C++ side builds (server/requests.cpp and server/test_interop.cpp).
package expect

import (
	"math"
	"strconv"
	"strings"

	"typed-endpoints/interop/e2epb"
)

// Small is the benchmark's small request.
func Small() *e2epb.Person {
	return &e2epb.Person{Name: "Ada Lovelace", Id: 1815, Email: "ada@example.org"}
}

// Edge has UTF-8 of two and three bytes per character and the smallest int32.
func Edge() *e2epb.Person {
	return &e2epb.Person{Name: "été €", Id: math.MinInt32}
}

// Medium is the benchmark's medium request: nested and repeated fields of most kinds.
func Medium() *e2epb.Order {
	o := &e2epb.Order{
		OrderId:       "order-0000000042",
		CreatedUnixMs: 1793300000000,
		Customer:      &e2epb.Person{Name: "Grace Hopper", Id: 1906, Email: "grace@example.org"},
		Labels:        []string{"express", "gift-wrap", "fragile", "priority-customer", "eu-warehouse"},
		Note:          []byte(strings.Repeat("n", 256)),
		Priority:      -3,
		Checksum:      0x0123456789ABCDEF,
	}
	for i := 0; i < 12; i++ {
		it := &e2epb.Item{
			Id:        int64(100000 + i*7919),
			Name:      "item-" + strconv.Itoa(i) + "-standard-widget",
			Price:     9.99 + float64(i)*1.25,
			Available: i%3 != 0,
		}
		for t := 0; t < 4; t++ {
			it.Tags = append(it.Tags, int32(i*10+t))
		}
		o.Items = append(o.Items, it)
	}
	return o
}

// EdgeOrder has a present but empty nested message and repeated element, an empty repeated
// string, negative int64 and packed int32 values (ten-byte varints), and the extremes of sint32
// and fixed64.
func EdgeOrder() *e2epb.Order {
	return &e2epb.Order{
		CreatedUnixMs: -1,
		Customer:      &e2epb.Person{},
		Items: []*e2epb.Item{
			{Id: -5, Price: -1.5, Tags: []int32{-1, 0, math.MaxInt32, math.MinInt32}},
			{},
		},
		Labels:   []string{""},
		Priority: math.MinInt32,
		Checksum: math.MaxUint64,
	}
}

// Persons are the EchoPerson requests, the empty message included.
func Persons() []*e2epb.Person { return []*e2epb.Person{Small(), Edge(), {}} }

// Orders are the EchoOrder requests, the empty message included.
func Orders() []*e2epb.Order { return []*e2epb.Order{Medium(), EdgeOrder(), {}} }
