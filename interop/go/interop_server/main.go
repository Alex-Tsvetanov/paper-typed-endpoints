// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0

// Command interop_server serves bench.e2e.Echo with grpc-go on a free loopback port, which it
// writes to -port-file. Each request must equal one of the known messages (package expect), so the
// client's encoding is checked field by field here; the reply is the request, which the client
// checks after decoding it. It exits once -persons EchoPerson, -orders EchoOrder and -unknown other
// calls have arrived (status 0 when the counts match and no request differed), or with status 1
// after -timeout (design note section 6; functional, never timed).
package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	"typed-endpoints/interop/e2epb"
	"typed-endpoints/interop/expect"
)

type counter struct {
	want     int64
	handled  atomic.Int64
	failures atomic.Int64
	done     chan struct{}
	once     sync.Once
}

func (c *counter) count(n *atomic.Int64) {
	n.Add(1)
	if c.handled.Add(1) == c.want {
		c.once.Do(func() { close(c.done) })
	}
}

// echo decodes one request, checks it against the known messages and returns it as the reply.
func echo[M proto.Message](c *counter, n *atomic.Int64, fresh func() M, known []M) grpc.MethodHandler {
	return func(_ any, _ context.Context, dec func(any) error, _ grpc.UnaryServerInterceptor) (any, error) {
		defer c.count(n)
		req := fresh()
		if err := dec(req); err != nil {
			c.failures.Add(1)
			fmt.Fprintf(os.Stderr, "interop_server: decode: %v\n", err)
			return nil, err
		}
		for _, k := range known {
			if proto.Equal(req, k) {
				return req, nil
			}
		}
		c.failures.Add(1)
		fmt.Fprintf(os.Stderr, "interop_server: a %T request differs from every known message\n", req)
		return nil, status.Error(codes.InvalidArgument, "the request differs from every known message")
	}
}

// writePortFile writes the port through a temporary file and a rename, so a reader never sees a
// partial number.
func writePortFile(path string, port int) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, []byte(strconv.Itoa(port)), 0o644); err != nil {
		return fmt.Errorf("write %s: %w", tmp, err)
	}
	if err := os.Rename(tmp, path); err != nil {
		return fmt.Errorf("rename to %s: %w", path, err)
	}
	return nil
}

func main() {
	portFile := flag.String("port-file", "", "where to write the listening port")
	persons := flag.Int64("persons", 0, "EchoPerson calls to expect")
	orders := flag.Int64("orders", 0, "EchoOrder calls to expect")
	unknown := flag.Int64("unknown", 0, "calls of unknown methods to expect")
	timeout := flag.Duration("timeout", 30*time.Second, "fail when the calls have not arrived by then")
	flag.Parse()

	c := &counter{want: *persons + *orders + *unknown, done: make(chan struct{})}
	var nPersons, nOrders, nUnknown atomic.Int64
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Fprintf(os.Stderr, "interop_server: %v\n", err)
		os.Exit(2)
	}
	s := grpc.NewServer(grpc.UnknownServiceHandler(func(any, grpc.ServerStream) error {
		defer c.count(&nUnknown)
		return status.Error(codes.Unimplemented, "unknown method")
	}))
	s.RegisterService(&grpc.ServiceDesc{
		ServiceName: "bench.e2e.Echo",
		HandlerType: (*any)(nil),
		Methods: []grpc.MethodDesc{
			{MethodName: "EchoPerson", Handler: echo(c, &nPersons, func() *e2epb.Person { return &e2epb.Person{} }, expect.Persons())},
			{MethodName: "EchoOrder", Handler: echo(c, &nOrders, func() *e2epb.Order { return &e2epb.Order{} }, expect.Orders())},
		},
		Metadata: "e2e.proto",
	}, struct{}{})
	go func() { _ = s.Serve(lis) }()
	port := lis.Addr().(*net.TCPAddr).Port
	if err := writePortFile(*portFile, port); err != nil {
		fmt.Fprintf(os.Stderr, "interop_server: %v\n", err)
		s.Stop()
		os.Exit(2)
	}
	fmt.Printf("interop_server: listening on 127.0.0.1:%d\n", port)

	select {
	case <-c.done:
		s.GracefulStop()
	case <-time.After(*timeout):
		s.Stop()
		fmt.Fprintf(os.Stderr, "interop_server: %d of %d calls by the timeout\n", c.handled.Load(), c.want)
		os.Exit(1)
	}
	fmt.Printf("interop_server: %d EchoPerson, %d EchoOrder, %d unknown, %d failures\n",
		nPersons.Load(), nOrders.Load(), nUnknown.Load(), c.failures.Load())
	if c.failures.Load() > 0 || nPersons.Load() != *persons || nOrders.Load() != *orders || nUnknown.Load() != *unknown {
		os.Exit(1)
	}
}
