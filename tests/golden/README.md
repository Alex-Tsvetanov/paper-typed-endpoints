# Golden wire vectors

`*.bin` are the protobuf encodings of the text-format inputs `*.txtpb` under the schemas `*.proto`,
made by `regen.sh` with `protoc --encode`. The tests compare this repository's encoder output with
them byte for byte and decode them.

Current files: made on 2026-10-23 with `libprotoc 3.21.12` (Ubuntu package
`protobuf-compiler 3.21.12-8.2ubuntu0.3`) from the local image `ctpb-dev:latest` (image id
bfa32b62a446). That `protoc` is a development oracle only, not the pinned version of the
paper. Before any freeze the files are regenerated with the pinned `protoc` (STATE.md, Lab
request 1), and any difference is investigated.

| File | sha256 |
|---|---|
| everything.bin | 31166881e8a84d9e4c5d6b609c8d96460c5ea188e9ac88a0a41d0c796e904c23 |
| hello_request.bin | 845bbb8f10df25cf8be443468acc4b7fbf687b76ad7bb3023e9f033b94f3cd23 |
| hello_reply.bin | 86c530bfe2b79bbbe606369e9c17664b64b6c4a017b203000ef27e351d183370 |

The inputs avoid a singular `-0.0` double: whether `protoc` writes it depends on the protobuf
version, so it is not a stable golden.
