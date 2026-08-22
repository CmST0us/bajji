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
| `22` | TIME_SYNC | 8-byte unsigned Unix epoch seconds, big-endian |
| `30` | CLEAR_BOND | Empty; Device clears its bond, rotates its BLE identity and disconnects |
| `31` | SETTINGS_GET | Empty |
| `32` | SETTINGS_SET | 1-byte version, 1-byte brightness, 1-byte display mode, 2-byte auto-refresh minutes |
| `33` | SETTINGS_STATE | 1-byte status followed by the 5-byte SETTINGS_SET payload |
| `40` | WALLPAPER_BEGIN | 1-byte version, 1-byte format, 4-byte total size, 4-byte CRC32 |
| `41` | WALLPAPER_CHUNK | 4-byte offset followed by 1–1276 bytes |
| `42` | WALLPAPER_COMMIT | Empty |
| `43` | WALLPAPER_CANCEL | Empty |
| `44` | WALLPAPER_RESULT | 1-byte request type, 1-byte status, 4-byte accepted byte count |
| `7F` | ERROR | 2-byte error code |

The byte stream may split or coalesce frames. A receiver may discard bytes
before the next magic word, but closes the CoC after a header with an invalid
version, type, or payload length. There is no payload CRC because LE L2CAP is
already ordered, reliable, and protected by a link CRC.

Each direction queues no more than 32 complete frames. CoC MTU is at least
1536. IPv4 MTU is 1280.

## Device settings

`SETTINGS_SET` payload byte 0 is version `01`. Byte 1 is display brightness
from 10 through 100 percent. Byte 2 is display mode: cover `00`, fit with a
blurred backdrop `01`. Bytes 3–4 are the automatic refresh interval in minutes;
zero disables automatic refresh and the maximum is 1440. `SETTINGS_STATE`
echoes these five bytes after a status byte. The response sequence matches the
request sequence and status zero means the settings were applied and persisted.

## Wallpaper transfer

Only one wallpaper transaction may be active. `WALLPAPER_BEGIN` version is
`01`; format is JPEG `01`, PNG `02`, GIF `03`, or WebP `04`; total size is
1–3,145,728 bytes. CRC32 is the IEEE value used by Ethernet and zlib. Every
chunk offset must equal the byte count accepted so far. The Device answers each
begin, chunk, commit, and cancel with `WALLPAPER_RESULT`; its byte count is the
next required offset. Status values are success `00`, invalid state `01`,
invalid argument `02`, storage failure `03`, checksum mismatch `04`, unsupported
media `05`, and busy `06`.

Commit succeeds only after exact size, CRC32, file-format, dimensions, and
decoder-budget checks. The temporary file then atomically replaces the current
cache. Cancellation, disconnection, or any failed commit deletes only the
temporary file, leaving the current wallpaper intact.

## BLE discovery

- Primary service UUID: `6f8f8db0-9c86-4ac5-a854-3a9e2f20b321`
- BridgeInfo characteristic UUID: `6f8f8db0-9c86-4ac5-a854-3a9e2f20b322`
- LE credit-based L2CAP PSM: `0x0081`

BridgeInfo is readable only on the encrypted bonded link and is exactly 22
bytes: protocol version, capability bits (`0x01` IPv4, `0x02` TCP, `0x04` UDP,
`0x08` device settings, `0x10` wallpaper transfer), big-endian
PSM, big-endian maximum payload, then the stable 16-byte Device ID.

## Wi-Fi provisioning

The encrypted, bonded GATT service also exposes writable characteristic
`6f8f8db0-9c86-4ac5-a854-3a9e2f20b323`. A write contains one network:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Version `01` |
| 1 | 1 | Security: open `00`, WEP `01`, WPA `02`, OWE `03`, WPA2 `04`, WPA3 `05` |
| 2 | 1 | SSID length, 1–32 |
| 3 | 1 | Password length, 0–64 |
| 4 | variable | SSID bytes, then UTF-8 password bytes |

The write is rejected unless the LE link uses Secure Connections, is bonded to
the saved peer, and carries a valid credential length. The device stores the
accepted station configuration through ESP-IDF and immediately attempts Wi-Fi.
Wi-Fi is the default route while it has an address; otherwise the BLE IP bridge
is the default route.
