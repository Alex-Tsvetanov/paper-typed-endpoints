// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0

// Command interop_client calls every rpc of bench.e2e.Echo on a server with grpc-go and checks
// every field of every reply against the request it sent (an echo), and that an unknown method
// is UNIMPLEMENTED. Exit status 0 when every check passes (design note section 6; functional,
// never timed). With -write-json it only writes the small and medium requests as JSON, ghz's
// request data, and exits.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"

	"typed-endpoints/interop/e2epb"
	"typed-endpoints/interop/expect"
)

const callTimeout = 10 * time.Second

func writeJSON(dir string) error {
	for name, m := range map[string]proto.Message{"person.json": expect.Small(), "order.json": expect.Medium()} {
		b, err := protojson.Marshal(m)
		if err != nil {
			return fmt.Errorf("%s: %w", name, err)
		}
		if err := os.WriteFile(filepath.Join(dir, name), b, 0o644); err != nil {
			return fmt.Errorf("%s: %w", name, err)
		}
	}
	return nil
}

func main() {
	addr := flag.String("addr", "127.0.0.1:50051", "server address")
	jsonDir := flag.String("write-json", "", "write person.json and order.json here and exit")
	flag.Parse()

	if *jsonDir != "" {
		if err := writeJSON(*jsonDir); err != nil {
			fmt.Fprintf(os.Stderr, "interop_client: %v\n", err)
			os.Exit(2)
		}
		return
	}

	conn, err := grpc.NewClient(*addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		fmt.Fprintf(os.Stderr, "interop_client: %v\n", err)
		os.Exit(2)
	}
	defer conn.Close()

	calls, failures := 0, 0
	invoke := func(method string, req, reply proto.Message) error {
		ctx, cancel := context.WithTimeout(context.Background(), callTimeout)
		defer cancel()
		calls++
		return conn.Invoke(ctx, "/bench.e2e.Echo/"+method, req, reply)
	}
	echo := func(method string, req, reply proto.Message) {
		if err := invoke(method, req, reply); err != nil {
			failures++
			fmt.Fprintf(os.Stderr, "interop_client: %s: %v\n", method, err)
		} else if !proto.Equal(req, reply) {
			failures++
			fmt.Fprintf(os.Stderr, "interop_client: %s: the reply differs from the request\n", method)
		}
	}

	for _, p := range expect.Persons() {
		echo("EchoPerson", p, &e2epb.Person{})
	}
	for _, o := range expect.Orders() {
		echo("EchoOrder", o, &e2epb.Order{})
	}
	if err := invoke("Missing", expect.Small(), &e2epb.Person{}); status.Code(err) != codes.Unimplemented {
		failures++
		fmt.Fprintf(os.Stderr, "interop_client: Missing: want UNIMPLEMENTED, got %v\n", err)
	}

	fmt.Printf("interop_client: %d calls, %d failures\n", calls, failures)
	if failures > 0 {
		os.Exit(1)
	}
}
