# ftnpkt

`ftnpkt` is a small command-line utility for creating, appending messages to,
and inspecting FidoNet packet (`.pkt`) files. It supports the three common
packet variants:

- **Type-2 "Stone Age"** (FTS-0001)
- **Type-2e** (FSC-0039)
- **Type-2+** (FSC-0048)

Character-set conversion of message bodies and header string fields is handled
via `iconv`.

## Requirements

- A C11 compiler
- CMake ≥ 3.11
- `iconv` (part of glibc on Fedora/RHEL; `libiconv` on macOS/musl)

## Building

```sh
make            # Release build into ./bin/ftnpkt
make debug      # Debug build into ./build
make test       # Build + create/addmsg/dump smoke round-trip
```

Or with plain CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Usage

```
ftnpkt <command> [options]
```

| Command | Description |
| --- | --- |
| `create <file.pkt> [options]` | Create an empty packet (header only). |
| `addmsg <file.pkt> <text> [options]` | Append a message to an existing packet. |
| `dump <file.pkt> [options]` | Dump the packet header and all messages. |

Run `ftnpkt <command> -h` for the options of a given command.

### Example

```sh
ftnpkt create out.pkt --from-addr 2:382/736 --to-addr 2:382/999 --password pwd
ftnpkt addmsg out.pkt 'Hello, FidoNet!' --from Jegor --from-addr 2:382/736 \
    --subject Test --areatag LINUX.GEN --charset cp866 --addkludge 'CHRS: CP866 2'
ftnpkt dump out.pkt
```

## Packaging

```sh
make rpm        # Source + binary RPMs from a fresh tarball
```
