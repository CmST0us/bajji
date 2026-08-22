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
| `01` | HELLO | 16-byte Device ID, 2-byte MTU, 4-byte session nonce, optional 1-byte role |
| `02` | HELLO_ACK | 2-byte accepted MTU, 4-byte echoed nonce, 1-byte status |
| `10` | IPV4 | One IPv4 packet, 20–1280 bytes |
| `20` | PING | 8-byte monotonic timestamp |
| `21` | PONG | The same 8-byte timestamp |
| `22` | TIME_SYNC | 8-byte unsigned Unix epoch seconds, big-endian |
| `30` | CLEAR_BOND | Empty; Device clears its bond, rotates its BLE identity and disconnects |
| `31` | SETTINGS_GET | Empty |
| `32` | SETTINGS_SET | 1-byte version, 1-byte brightness, 1-byte display mode, 2-byte auto-refresh minutes |
| `33` | SETTINGS_STATE | 1-byte status followed by the 5-byte SETTINGS_SET payload |
| `34` | NETWORK_GET | Empty |
| `35` | NETWORK_SET | Version and mode, followed by manual credentials when required |
| `36` | NETWORK_STATE | Status, version, mode, link state, RSSI, error, and SSID |
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

The original 22-byte `HELLO` remains a data-role handshake. A 23-byte `HELLO`
uses byte 22 to declare data role `00` or control-only role `01`. A control-only
CoC can carry settings, network control, time synchronization, and wallpaper
messages, but the Device must never attach it to the IPv4 bridge.

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

## Network selection

Network mode is mutually exclusive and persists on the Device: unset `00`,
manual Wi-Fi `01`, shared iPhone Wi-Fi `02`, or VPN `03`. `NETWORK_SET` begins
with version `01` and the mode. Shared and VPN payloads end after those two
bytes. Manual Wi-Fi appends security, SSID length, password length, SSID bytes,
and password bytes using the security values and limits from Wi-Fi provisioning
below. Passwords are never returned by the Device.

`NETWORK_STATE` begins with a request status byte followed by version `01`, the
selected mode, link state, signed RSSI byte, signed 32-bit error code, SSID
length, and 0–32 SSID bytes. Link state is disabled `00`, unconfigured `01`,
awaiting credentials `02`, connecting `03`, connected `04`, or retrying `05`.
The response sequence matches the request sequence.

## BLE discovery

- Primary service UUID: `6f8f8db0-9c86-4ac5-a854-3a9e2f20b321`
- BridgeInfo characteristic UUID: `6f8f8db0-9c86-4ac5-a854-3a9e2f20b322`
- LE credit-based L2CAP PSM: `0x0081`

BridgeInfo is readable only on the encrypted bonded link and is exactly 22
bytes: protocol version, capability bits (`0x01` IPv4, `0x02` TCP, `0x04` UDP,
`0x08` device settings, `0x10` wallpaper transfer, `0x20` network control,
`0x40` control-only CoC), big-endian
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
the saved peer, and carries a valid credential length. The device accepts this
write only while shared mode is awaiting credentials, or while migrating a
legacy unset configuration. It stores the accepted station configuration and
attempts Wi-Fi without changing to an unselected upstream. Captive Portal
success selects manual mode. Manual and shared modes use only Wi-Fi; VPN mode
stops the station and uses only a data-role CoC.
