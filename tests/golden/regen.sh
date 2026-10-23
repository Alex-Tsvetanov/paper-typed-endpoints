#!/usr/bin/env bash
# Regenerates the golden wire vectors with a given protoc. Usage: regen.sh <protoc>
set -euo pipefail
protoc="$1"
cd "$(dirname "$0")"
"$protoc" --version
"$protoc" -I. --encode=tep.test.Everything everything.proto < everything.txtpb > everything.bin
"$protoc" -I. --encode=tep.demo.HelloRequest greeter.proto < hello_request.txtpb > hello_request.bin
"$protoc" -I. --encode=tep.demo.HelloReply greeter.proto < hello_reply.txtpb > hello_reply.bin
sha256sum everything.bin hello_request.bin hello_reply.bin
