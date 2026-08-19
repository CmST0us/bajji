# Bajji Bridge v1

Bridge v1 carries one message in an 8-byte header followed by its payload. All
multi-byte integers use network byte order.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `BA 77` |
| 2 | 1 | Version `01` |
| 3 | 1 | Type |
| 4 | 2 | Payload length |
| 6 | 2 | Sequence number, modulo 65536 |

Types and payloads:

| Type | Name | Payload |
|---:|---|---|
| `01` | HELLO | 16-byte Device ID, 2-byte MTU, 4-byte session nonce |
| `02` | HELLO_ACK | 2-byte accepted MTU, 4-byte echoed nonce, 1-byte status |
| `10` | IPV4 | One IPv4 packet, 20–1280 bytes |
| `20` | PING | 8-byte monotonic timestamp |
| `21` | PONG | The same 8-byte timestamp |
| `7F` | ERROR | 2-byte error code |

The byte stream may split or coalesce frames. A receiver may discard bytes
before the next magic word, but closes the CoC after a header with an invalid
version, type, or payload length. There is no payload CRC because LE L2CAP is
already ordered, reliable, and protected by a link CRC.

Each direction queues no more than 32 complete frames. CoC MTU is at least
1536. IPv4 MTU is 1280.
