# w2b design notes

What the code is built on: the decisions, the limits, and the facts of the
platform and the libraries it depends on. `README.md` says what the relay does
and how to use it; `CLAUDE.md` the tooling, the layout and the coding style.
Constants are named as in the code (`TcpServer::kRxRingBytes`); a value in
brackets is the one at the time of writing, the code is the source.

## 1. Why its own firmware

Nothing published bridges TCP to an arbitrary GATT write/indicate pair with
rules made at run time:

- ESPHome's `bluetooth_proxy` gives a host's Bluetooth stack a remote adapter,
  not a TCP channel, and three active connections per proxy.
- esp32-ble2mqtt is a generic GATT client with indications and OTA, but MQTT
  per characteristic: no stream, no web page.
- The UART-to-TCP stream servers (esphome-stream-server, esphome-uart-link)
  and the BLE-to-UART bridges (esp32-ble-uart-mx) each do one half.
- ESP-IDF's `blecent` and `ble_spp_client` are skeletons.

ESPHome as the framework was ruled out: the whole product (rule store, BLE
central on its own stack, TCP servers, scan and probe API, the page) would be
an external component anyway; ESPHome's BLE components would have to stay out
of the YAML while NimBLE is set up next to its WiFi handling; per-relay
entities are impossible because entities are compile-time; and its web server
plus BLE on a classic ESP32 is memory-tight. What it would have given (WiFi
manager, captive portal, OTA, mDNS, logging, status LED) is a few hundred
lines with mature Arduino libraries.

## 2. Decisions

- **Transparent and generic.** No protocol knowledge: no parsing, no padding
  stripping, no checksums, no filtering. Nothing in the firmware knows any
  particular device.
- **Multi-client policy: broadcast.** Every connected TCP client receives
  every BLE indication; client packets go onto the BLE link whole, one at a
  time. `max_clients` per rule, default 1.
- **ATT MTU: NimBLE's preferred 255**, global, no per-rule setting; the
  negotiated value is in the status. Oversize policy per rule: `chunk`
  (default), `long_write` or `drop`.
- **Connect policy per rule:** `on_demand` (the link comes up with the first
  TCP client and goes `idle_s` after the last one left) or `always`. A rule
  is enabled or disabled, nothing else; disabling drops its TCP clients and
  its BLE link at once.
- **Device exclusivity.** While the relay holds a device's link nothing else
  can connect to that device, a phone app for one: `on_demand` frees it
  `idle_s` after the last client left.
- **LAN trust** on the data ports, no access control. A client-IP allowlist
  is a possible later addition.
- **Toolchain:** PlatformIO, Arduino 3.x on ESP-IDF 5.x through the
  pioarduino platform, NimBLE-Arduino, the ESP-IDF and NimBLE C APIs directly
  where needed.
- **Auto channel selection runs in the page**, on the probed GATT table, when
  the rule is saved (section 6). The relay stores and runs UUIDs only. An Auto
  that finds nothing saves None after the user confirms.
- **Board facts are build flags** of the board's environment (LED pin and
  polarity, the battery divider), never in `config.json`.
- **Per-project PlatformIO `core_dir`** `${PROJECT_DIR}/.pio-core`: the
  pioarduino platform installs its fork of PlatformIO Core and its Python
  tools into `<core_dir>/penv` on every build; a project-local core_dir keeps
  that out of `~/.platformio/penv`, where the official core and the `pio`
  executable stay (the arrangement ESPHome uses).
- **Threads.** Modules are `Component`s: `setup()` + `update(dtMs)`. A
  `Scheduler` owns one FreeRTOS thread pinned to a core and runs *tasks*,
  plain callables registered with `every(periodMs, task)` /
  `once(delayMs, task)`; it runs the due ones in order, passes each the real
  milliseconds since its previous run, and sleeps until the next is due, at
  most `TaskList::kMaxSleepMs` (100 ms); the logic is `TaskList` in
  `lib/core`, host-tested. Two schedulers: `relay` on core 1, the transport
  (BLE scan and connections, the TCP servers); `application` on core 0 beside
  the radio stacks, everything else. Nothing blocks a scheduler: a module that
  waits checks again on its next update. Blocking work would get its own
  `Thread`; none does. Shared modules take a `Lock`. Arduino's `loop()` is
  not used. One PlatformIO env per hardware, named after it.
- **WiFi fallback and setup.** The setup AP (`PORTAL_AP_SSID`, `w2b-setup`)
  is open. While a client is on the AP, station retries pause; without one,
  the station keeps retrying beside the AP. The portal's Connect saves, drops
  the AP and joins alone; without a link after `WifiSupervisor::kJoinLimitMs`
  (30 s) the AP comes back; a refusal the driver reports (password rejected,
  network not found) brings it back at once, and the setup page says why. The
  WiFi password is stored in plain text. A rule's export holds that rule
  only; the full export is the whole configuration, WiFi password included.
- **Data classes serialize themselves**: private fields, `const` getters,
  public constructors that set every field. With a valid default state the
  default constructor is public and `fromJson` keeps the current value of a
  key the JSON leaves out; without one it is private, reached through
  `SerializableFactory::createEmpty<T>()`, and the keys are required.
  Persisted classes implement `IJsonSerializable<TSelf>` (`toJson` /
  `fromJson` at their own nesting level, errors as "field: reason" with the
  parent's key prefixed). Headers forward-declare ArduinoJson only. A
  corrupted `/config.json` is kept as `/config.json.corrupted`.
- **Web pages are files in `web/`, built into the firmware image**:
  `scripts/web_assets.py` inlines the linked stylesheet and scripts, minifies
  by line and gzips every page into a generated header; `lib/web/WebPages`
  serves them with `Content-Encoding: gzip`. Page and API always come from
  the same build. Not on LittleFS: `uploadfs` rewrites the whole partition,
  `/config.json` included.
- **The configuration owns its lock and its changes.** `Config` is the one
  running configuration: reads return copies or run under its lock; a change
  is `submit(what, edit, followUp)`, the edit working in place on its staging
  copy, then `update()` on the application thread saves it, puts it in use
  and runs the follow-up. Its content is the value class `ConfigData`. Text
  of a known limit is a `FixedString<N>`, lists of a known limit
  `std::array`, addresses and UUIDs bytes; the rule list, of no fixed length,
  a `std::vector`.
- **One BLE link per device, shared by its rules.** Enabled rules on the same
  device share one connection, up while any of them wants it; no device level
  in the configuration. A target with a name and no address is the strongest
  device advertising the name, with a warning when more than one does. Two
  rules on one TX are allowed; packets never interleave.
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` (9) is the controller's limit: one per
  enabled rule's device (`ConfigData::kMaxActiveRules`, one less) and one for
  the probe.
- **The probe disconnects as soon as it has read the table**, so the device
  is free again when the page shows it.
- **BLE without threads.** `BleConnectionManager` holds one `BleConnection`
  per device of the enabled rules; each connection and the probe are state
  machines moved by `update()`, on NimBLE's C calls (`ble_gap_*`,
  `ble_gattc_*`), none of which block; NimBLE's callbacks only store data.
  `BleScanner` is the one entry to NimBLE's scan and shares the radio
  (section 6).
- **TCP on lwIP sockets in the relay thread**: non-blocking BSD sockets,
  polled by the relay scheduler, never AsyncTCP, whose one task also runs the
  web server's handlers.
- **TCP limits and buffers.** lwIP's 16 sockets and 16 TCP connections (the
  web server's included) are checked at run time only: a listener that cannot
  open retries and the status says why; a client beyond `max_clients` or
  without a socket is refused, logged and shown. The relay thread runs every
  `RELAY_UPDATE_MS` (10 ms), the most the relay adds each way. Buffers: per
  client `TcpClientData::kCapacity` (1 KB), per rule one RX ring of
  `TcpServer::kRxRingBytes` (1 KB) shared by its clients, both a
  `PacketRing`. A client's socket is always read; bytes beyond its full
  buffer are dropped and counted. Every client socket has TCP keepalive
  (`TcpServer::kKeepAliveIdleS`, `kKeepAliveIntervalS`, `kKeepAliveCount`):
  lwIP resets a client that stopped answering and frees its queues at once. A
  client whose socket takes nothing for `TcpServer::kSendStallMs` (15 s, past
  the keepalive's deadline) does not read and is closed.
- **No cap on the clients across the rules.** `max_clients` per rule is the
  only limit; the sum over the enabled rules is not checked. Too many heavy
  clients exhaust the heap and the relay restarts (section 14); that is
  accepted.
- **mDNS: the hostname and `_http._tcp` only**; no records for the rules.

## 3. Hardware and toolchain

| | |
|---|---|
| Board | TZT "ESP32 LOLIN32" clone with the LOLIN32 Lite layout: ESP32-D0WDQ6-V3 (revision v3.1), dual core 240 MHz, 40 MHz crystal, 4 MB flash, no PSRAM. Board id `lolin32_lite`, env `TZT-ESP32-Lite` |
| LED | GPIO22, active-low: `-D LED_GPIO=22 -D LED_ACTIVE_LOW=1` in the board's env. The blue LED by the LiPo connector is the charger's CHRG output, not on a GPIO; it blinks whenever USB is present without a battery |
| Battery | Optional: battery plus, 100 k, GPIO34, 100 k, GND, 100 nF across the lower resistor: `-D BATTERY_GPIO=34 -D BATTERY_DIVIDER_TOP_KOHM=100 -D BATTERY_DIVIDER_BOTTOM_KOHM=100`; without `BATTERY_GPIO` no battery. `BatteryMonitor` reads the pin (11 dB, `analogReadMilliVolts`) every `BATTERY_UPDATE_MS` on the application thread; the voltage is the mean of the last `kSamples` through the divider, times `device.battery_scale_ppm`; below `kNoBatteryBelowMv` it reads as no battery; a debug line every `kLogPeriodMs` |
| Platform | pioarduino 55.03.312 = Arduino core 3.3.12, ESP-IDF 5.5.5, xtensa-esp-elf 14.2.0, esptool 5.4.0, pinned by its release URL in `platformio.ini`; `core_dir` = `${PROJECT_DIR}/.pio-core` |
| Host build, env `native` | a `g++` on PATH; both envs compile as gnu++2b |
| Partitions | `min_spiffs.csv`: two 1920 KB OTA app slots; a 128 KB data partition `spiffs` at 0x3D0000, used as LittleFS for `/config.json` |
| Flash | 80 MHz, QIO: `board_build.f_flash = 80000000L`, `board_build.flash_mode = qio` in the board env; a serial upload applies them |
| Monitor | 115200, `monitor_filters = esp32_exception_decoder` |
| Libraries | NimBLE-Arduino 2.5.1; ESP32Async ESPAsyncWebServer 3.12.1 and AsyncTCP 3.5.0; ArduinoJson 7.4.3; x-stuff SimpleProfiler 1.1.0 (its counters compile in with `ENABLE_PROFILE=1`, printing through `PROFILER_LOG_FUNCTION=printf`; off in a release build); the core's LittleFS, ESPmDNS, DNSServer, Update, WiFi |
| Build flags | `[common]`, both envs: `FW_VERSION`, `HTTP_PORT=80`, `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9`. Board env: `CORE_DEBUG_LEVEL=3`, `USE_ESP_IDF_LOG`, `ARDUHAL_ESP_LOG_TAG="arduino"`, `LOG_LOCAL_LEVEL=3` (section 13), the LED pair, the battery trio, NimBLE central and observer only (`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED`, `..._BROADCASTER_DISABLED`), `CONFIG_NIMBLE_CPP_LOG_LEVEL=2`, `CONFIG_ASYNC_TCP_STACK_SIZE=8192` |
| Not used | WiFiManager (its synchronous web server conflicts with the async one); ElegantOTA |
| Size, firmware 0.3.3 | static RAM 76 KB (23 %), flash 77 % of the 1920 KB slot |

## 4. Configuration

One rule is one relay. `/config.json` on LittleFS, with a `schema` version
(`ConfigData::kSchema`). An example with the defaults:

```json
{
  "schema": 1,
  "device": { "hostname": "w2b", "log_level": "debug", "timezone": "UTC", "use_random_ble_address": false, "heartbeat_s": 15, "battery_scale_ppm": 1000000 },
  "wifi": { "ssid": "...", "password": "...", "static": null },
  "rules": [
    {
      "id": "id0",
      "name": "Relay 1",
      "enabled": true,
      "listen_port": 8899,
      "max_clients": 1,
      "target": { "address": "12:34:56:78:9a:bc", "name": "My device" },
      "service_uuid": "fff0",
      "tx": { "mode": "uuid", "uuid": "fff1", "write_with_response": true },
      "rx": { "mode": "uuid", "uuid": "fff2", "subscribe": "indicate" },
      "coalesce_ms": 10,
      "oversize": "chunk",
      "connect_policy": "on_demand",
      "idle_s": 3,
      "reconnect_backoff_s": [2, 4, 8, 10, 30],
      "close_tcp_on_ble_loss": false,
      "hexdump": false
    }
  ]
}
```

Loading: `schema` is required; another key the file leaves out takes its
default, except in `wifi.static`, where `ip`, `gateway` and `subnet` are
required and `dns` is optional (`"static": null` is DHCP); an unknown key is
ignored; a wrong type or an out-of-range value makes the file corrupted: it
is kept as `/config.json.corrupted` and the defaults are written. The file
holds at most `ConfigStore::kMaxBytes` (28 KB); a bigger save is refused. A
save writes a new file whole and swaps it in, so a power cut during a save
keeps the old one.

Device and WiFi: hostname an RFC 1123 label; `log_level` error, warn, info
or debug; `timezone` a zone of `lib/time/TimeZones`; `use_random_ble_address`
the relay's own BLE address type (section 6); `heartbeat_s` seconds between
two health lines, 0 none, at most `DeviceConfig::kMaxHeartbeatS`;
`battery_scale_ppm` the factor on the battery's voltage through the divider,
parts per million, `kUncalibratedBatteryScalePpm` none, at most
`kMaxBatteryCorrectionPpm` (10 %) away from it. SSID up to 32 bytes; password
empty, 8 to 63 printable ASCII characters, or 64 hex digits; a static address
is a strict dotted quad with a contiguous mask and the gateway in its subnet.

Rules:

- Any number stored; at most `ConfigData::kMaxActiveRules` enabled. `target`
  (an `address`, a `name` or both) and `service_uuid` are required; every
  other key takes the default above. `id` is optional: a rule without one
  gets the first free of `id0`, `id1`, ...; unique, 1 to 32 letters, digits,
  `-`, `_`.
- `listen_port` 1 to 65535, not the web port (`RuleConfig::kWebPort`), unique
  among the enabled rules; default `RuleConfig::kDefaultPort` (8899). Enabled
  rules may share a device, on other characteristics of it.
- `name` and `target.name` at most 63 bytes, no control characters;
  `target.address` six hex bytes with colons, either case, `""` for none. No
  address type: the connect takes it from the advertisement (section 6).
- `max_clients` 1 to `RuleConfig::kMaxClients` (4).
- UUIDs 16-bit, 32-bit or 128-bit with dashes, kept in their written form.
  `tx.mode` and `rx.mode` are `uuid` or `none`; `none` when left out, for a
  write-only or read-only relay, not both; `uuid` required with mode `uuid`.
  `tx.write_with_response` a bool; `rx.subscribe` `auto`, `indicate` or
  `notify`, `auto` indicating when the characteristic has Indicate.
- `oversize` `chunk`, `long_write` or `drop`; `connect_policy` `on_demand`
  or `always`; `coalesce_ms` 0 to `RuleConfig::kMaxCoalesceMs`; `idle_s` 0
  to `kMaxIdleS`; `reconnect_backoff_s` 1 to `kMaxBackoffSteps` steps of 1
  to `kMaxBackoffS` seconds.
- A text longer than its field (id 32, names and hostname 63, SSID 32,
  password 64 bytes) is refused as "key: longer than N bytes". A file's
  `rules` replaces the whole list; without the key the list stays.

Defaults in one line: MTU 255, coalesce 10 ms, oversize chunk, max_clients 1,
on_demand with idle 3 s, backoff 2, 4, 8, 10, 30 s, broadcast.

## 5. Relay semantics

TCP to BLE:

1. A client's bytes grow an open packet in its `TcpClientData`. The packet
   closes after `coalesce_ms` of silence (0: at once), at MTU - 3 with
   `chunk`, or when the buffer is full.
2. Closed packets are written whole, one at a time per device, round robin
   over the device's rules and their clients, so packets never interleave.
   The mode follows `chooseWriteMode()` (`lib/relay/WriteMode`): up to
   MTU - 3 one write; above it, `chunk` in consecutive writes of MTU - 3,
   `long_write` as one ATT long write up to `WriteMode::kMaxAttributeValue`
   (512 B, beyond it dropped), `drop` dropped; drops count as
   `oversize_events`. With response when the rule asks and the
   characteristic offers it, else without. When NimBLE is out of buffers the
   write waits for the next update.
3. While the link is not up, packets wait up to `TcpServer::kQueueTimeoutMs`
   (10 s), then are dropped and counted. The socket is read at every update,
   link up or down; bytes beyond a full buffer are dropped and counted
   (`tx_dropped`), with a log line when it starts and one with the total once
   the buffer ran empty.
4. Without TX (`tx.mode` `none`) the bytes are discarded and counted.

BLE to TCP:

5. Each indication or notification goes into the rule's RX ring, unchanged,
   and from there to **every** client of the rule. Each client reads at its
   own place; a packet that does not fit drops the oldest whole packets, read
   or not, and a client behind counts what it lost (`rx_dropped`). A client's
   socket gets as many bytes as it takes; lwIP takes part or none while its
   send queue is full (at most 16 segments, `TCP_SND_QUEUELEN`, before the
   peer's delayed ACK), and the rest follows at the next update from the
   client's place in the ring. A packet dropped while partly sent counts as
   lost. Every client socket has TCP keepalive: after
   `TcpServer::kKeepAliveIdleS` without a segment from the client a probe
   goes every `kKeepAliveIntervalS`, and after `kKeepAliveCount` unanswered
   lwIP resets the connection and frees its queues. A client whose socket
   takes nothing for `TcpServer::kSendStallMs` does not read and is closed.
6. NimBLE confirms indications itself. Without RX (`rx.mode` `none`) nothing
   is subscribed.

Link management:

7. `on_demand`: the link is wanted while the rule has a client and for
   `idle_s` after the last one left. `always`: while the rule is enabled. The
   device's link is up while any of its rules wants it.
8. A failed attempt waits the backoff of the device's first rule, then tries
   again. Clients that never saw the link wait through the attempts until the
   next wait would be the backoff's last, then are closed (with the default,
   at the fifth failure, after the 10 s wait).
9. A lost link reconnects the same way; its clients stay open, except those
   of rules with `close_tcp_on_ble_loss`.
10. Disabling a rule drops its clients and, when no other rule wants it, the
    link at once.
11. MTU 255 is requested on every connect; the negotiated value is in the
    status.

## 6. BLE engine

- **Page scan** (`BleScanner`, `lib/ble/BleScanList`): active (for the
  names), 1 to `BleScanner::kMaxSeconds` (30) s, `kDefaultSeconds` (15)
  without a length; a scan window of `kScanWindowMs` (50 ms) every
  `kScanIntervalMs` (100 ms), the rest for WiFi; every report through the
  callbacks (duplicates on, so a later scan response and a stronger signal
  arrive), none stored by NimBLE. One entry per address: the strongest
  signal, the latest name and manufacturer data (`BleDevice::kMaxFieldBytes`
  each), the advertised services merged (`kMaxServices`), whether it is
  connectable; strongest first, no limit on the list. The page clears the
  relay's copy once it has it. Scan requests reach every device in range.
- **The radio.** NimBLE does one of scanning and connecting at a time, so
  `BleScanner` lends the radio to one holder, identified by a pointer: a
  connection or the probe until its connect has ended, the page scan while it
  runs. `acquireRadio(holder)` succeeds while the radio is free or already
  the holder's, and stops the search scan; `releaseRadio(holder)` frees it
  for its holder only, so every way out may release. A page scan and a probe
  wait for the radio up to `BleConnection::kRadioWaitMs` (twice
  `kConnectTimeoutMs`), then fail.
- **Finding the device.** Every connect first waits for the device's
  advertisement and takes the address type (public or random) from it; the
  configuration holds none. A connection's search (`startSearch()`,
  `readSearch()`, `stopSearch()`) runs for `BleScanner::kSearchTimeoutMs`
  (10 s) of scan time: by address, the first report; by name, the strongest
  device seen within `kNameWaitMs` (1 s) of the first, with a warning when
  another device has the name. The search scan runs, without a time limit,
  while a search runs and nobody holds the radio. Searches also see the page
  scan's reports, and their time counts while either scan runs; a device
  found during a page scan connects when the page scan has ended.
- **Rule connections** (`BleConnection`): per device, search, connect
  (`kConnectTimeoutMs`, 10 s), MTU exchange, all primary services, the
  characteristics of the rules' services, each RX's CCCD, the CCCD writes
  (indicate `0x0002`, notify `0x0001`), up. `ChannelHandles` (`lib/relay`)
  finds TX and RX by UUID in any form and checks their properties. A rule the
  device lacks something for shows `error` and the device's other rules run;
  with none left the attempt fails. A connect ends with NimBLE's connect
  event, or with its disconnect event alone when the link fails before NimBLE
  has read the device's features; a disconnect ends with its event, or after
  `kCloseWaitMs` (32 s, the longest supervision timeout) without one; late
  events of a connection given up are ignored. The MTU comes from the
  exchange, NimBLE's MTU event, or `ble_att_mtu()` when the device exchanged
  first. The link's RSSI is read every `kRssiPeriodMs` for the status. A
  device removed while its connection still closes gets no new connection
  until it has closed. Status states: disabled, idle, searching, connecting,
  discovering, subscribing, up, waiting, disconnecting, error.
- **Probe** (`BleDiscovery`, `lib/ble/GattTable`, the page's Probe): a state
  machine on the application scheduler. It takes the radio, connects to a
  device as the scan listed it (address, address type, connectable), no MTU
  exchange; reads the primary services and their characteristics with
  discovery requests, then the values of GAP's name (2A00) and appearance
  (2A01) and of Device Information's 2A29, 2A24, 2A25, 2A27, 2A26, 2A28 and
  2A50 where they offer read, the first `GattValue::kMaxBytes` of each,
  through `ble_gattc_read_long` (a guarded value is left out); writes
  nothing; disconnects at once. One probe at a time; the page scan and the
  probe take turns. A device's security request gets Just Works pairing,
  never a bond; how it ended is logged.
- **Auto selection**, in the page, inside the chosen service of the probed
  table:

| Channel | 1st choice | 2nd choice | Never silently |
|---|---|---|---|
| TX | properties exactly Write (0x08) | any characteristic with the Write bit | Write-No-Response only |
| RX | properties exactly Indicate (0x20) | Indicate bit, then Notify bit | anything that also has a write bit |

  The page always shows the resolved UUIDs and their properties.
- **Own address**: public by default; `device.use_random_ble_address` makes
  it NimBLE's random static one. The boot log and `/api/status` show it.
- **Connection parameters**: the peripheral's.
- **Connections**: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, one per enabled rule's
  device, the last for the probe.

## 7. TCP server

`TcpServer` (`lib/esp32`), one per enabled rule, owned by its device's
`BleConnection` and run by the relay thread: a listener on
`0.0.0.0:listen_port` with `SO_REUSEADDR`, retried every `kListenRetryMs`
(5 s) while it cannot open; up to `max_clients` clients, `TCP_NODELAY` and
keepalive, every further one closed at once. The semantics are section 5.
The status lists the clients (`ip:port`) and says in `tcp_error` why the
listener does not run or why the last client was refused.

## 8. Threads

Core 1 (APP_CPU) runs the `relay` scheduler, the transport; core 0 (PRO_CPU)
runs the radio stacks and the `application` scheduler, everything else. A
stall on one side does not hold up the other; a crash still resets the chip,
and a flash write (config save, OTA) pauses code in flash on both cores. The
application thread sets up every module, the relay's too, and starts the
relay scheduler once NimBLE runs.

| Task | Core | Runs |
|---|---|---|
| WiFi driver, lwIP, mDNS, `esp_timer`, BT controller (library) | 0 | the network and radio stacks, pinned there by the prebuilt sdkconfig |
| NimBLE host task (library) | 0 | NimBLE's callbacks; they only store data under a lock |
| `application` scheduler (`APPLICATION_STACK_BYTES`, `APPLICATION_PRIORITY`) | 0 | WiFi supervision, setup portal, mDNS, time, OTA, config save, restart, heartbeat, battery, LED, the probe |
| `relay` scheduler (`RELAY_STACK_BYTES`, `RELAY_PRIORITY`, every `RELAY_UPDATE_MS`) | 1 | BLE scan and searches (`BleScanner`), the connections and their TCP servers (`BleConnectionManager`) |
| Async web server on AsyncTCP's task (library) | any | HTTP handlers; they never block; a change goes through `Config::submit()` or a module's request under its lock |

## 9. Web page

One page, `web/index.html` (vanilla JS, no framework, no build step beyond
`scripts/web_assets.py`), views chosen by the URL fragment:

- **Relays** (default): the rules with their state, port, target, MTU,
  clients and counters; Enable/Disable, Edit, Export, Remove; Add.
- **Rule form**, in sections (General, TCP host, BLE: device, service, TX,
  RX, connection; Other), help under every field; a new rule starts from
  `GET /api/rules`' `defaults`, the field limits from its `limits`. Scan runs
  the relay's scan and lists the devices as they are heard; each row has
  Probe once the scan ended (greyed out when not connectable). Picking a
  device fills address, name and service; a device not probed is probed at
  once. With a table, the service is a list and TX and RX are Auto, a
  characteristic picked by hand, or None; every property bit of the chosen
  characteristic is shown. Save resolves Auto (section 6). A name the probe
  read but the device does not advertise fills the form's name; a rule that
  has no address cannot find the device by it. The HF-LPT270 preset, shown
  for a device of that name, fills that module's service `fee7`, TX `fed5`
  with response and RX `fed6` indicate; the page warns while a rule names
  `fed4`, the module's AT-command characteristic. The firmware refuses
  neither.
- **Status**: device with the battery (its voltage and an estimated charge
  from a lithium-ion cell's curve at rest), WiFi, time, the relays by name
  with their link.
- **Log**: follows `/api/log`, pause, clear view.
- **Config**: the configuration read-only, Export, Import, time zone, log
  level and heartbeat; the battery's calibration: the voltage a multimeter
  shows, Calibrate, Reset.
- **Firmware**: both slots, upload with a progress bar, Restart.

The page sends one request at a time, each waiting for the one before it.
The view in front polls at its own rate, the status every 5 s or the log
every 2 s, the other every 60 s; a hidden browser tab polls nothing, and
coming back fetches both at once. The status keeps its fast rate while a
restart or a new image's confirmation is awaited, in any view, since the
firmware, config and restart views learn of them from the status.

UUIDs, appearances and the vendors of member UUIDs show by name
(`web/ble-names.js`, from the SIG's assigned numbers, kept as it is).
`web/setup.html` is the setup page on the AP.

## 10. HTTP API

"LAN only" paths answer 403 on the setup AP, checked by the handler from the
address the request came in on. An unknown path under `/api/` answers 404 as
JSON, 415 for a `POST` or `PUT` without a JSON body; any other unknown URL
redirects to the page (captive portal).

| Method, path | Purpose |
|---|---|
| `GET /` | the page on the station's address, the setup page on the AP, by the address the request came in on; gzipped, with an `ETag` from its bytes: 304 and no body for a browser's `If-None-Match` |
| `GET /setup` | the setup page, on either address |
| `GET /api/status` | `device`: hostname (the one the station joined as), firmware, build, chip, uptime, heap free / lowest / largest block, `battery_mv` (0: no battery), `ble_address`, `ble_address_type` (public, random; "" while NimBLE does not run). `wifi`, the link as the driver last reported it (section 13): SSID, connected, address, mask, gateway, DNS, RSSI (the last sample, at most 1 s old), channel (0 while down), BSSID, MAC. `time`: `local` ("25/12/2026 15:38:42", empty until the clock is set), `timezone`, `synced`, `last_sync_s`. `ota`: both slots and their states, `confirm_after_s`. `relays`, one per rule in the configuration's order, at most `BleConnectionManager::kStatusPeriodMs` old: `id`, `name`, `state` (section 6), `address`, `mtu`, `rssi`, `up_s`, `error`, `rx_frames`, `rx_bytes`, `rx_dropped`, `tcp_rx_bytes`, `tcp_tx_bytes`, `ble_tx_writes`, `tx_dropped`, `oversize_events`, `reconnects`, `listening`, `tcp_error`, `clients` |
| `GET /api/rules`, `POST /api/rules`, `PUT /api/rules/{id}`, `DELETE /api/rules/{id}` | rules: `GET` `{rules, defaults, limits}` (`limits`: `enabled`, `clients`, `coalesce_ms`, `idle_s`, `web_port`); `POST` adds a rule under the first free `id<N>`, the body's `id` unused; `PUT` replaces the rule, keys left out take their defaults, a body's `id` must be `{id}`. 202 `{save: pending}`, then `GET /api/config/save`; 400 with `error`, 404 for an unknown id, 409 while another change saves, 415 without JSON; at most `RuleService::kMaxBodyBytes`; each change writes `/config.json`. LAN only |
| `GET /api/rules/{id}/export` | that rule's JSON as the download `<hostname>-rule-<id>.json`; a valid `POST` body. LAN only |
| `POST /api/ble/scan` `{seconds}` / `GET /api/ble/scan/results` / `POST /api/ble/scan/clear` | page scan: `POST` 202 `{state, seconds}`, `{}` for `kDefaultSeconds`, a scan requested or running is not restarted, 409 while a probe runs; results `{state, devices}` (idle, requested, running, done, failed; while running the devices so far), per device `address`, `address_type`, `connectable`, `name`, `rssi`, `services`, `manufacturer_data` (hex); clear frees the relay's copy, 409 while a scan runs. LAN only |
| `POST /api/ble/discover` `{address, address_type, connectable}` / `GET /api/ble/discover/result` | probe: the device as the scan listed it; 400 when not connectable; 202 `{state}`, 409 while a scan or another probe runs; result `{state, address, services, values}` (idle, requested, connecting, reading, done, failed), each service `{uuid, characteristics}`, each characteristic `{uuid, properties}` (the declaration's property byte), `values` `{uuid, value}` in hex. LAN only |
| `GET /api/log?since=N` | the log ring (`LOG_RING_BYTES`) from byte offset N, all of it without N, as plain text streamed from the ring in pieces (chunked); `X-Log-From` (the first byte's offset, above N when lines were dropped), `X-Log-Next` (the next N), `X-Log-Boot` (random per boot: offsets restart). LAN only |
| `GET /api/wifi` / `PUT /api/wifi` | the saved SSID and static address, never the password; `save` (idle, pending, saved, failed), `join` (none, joining, connected, password_rejected, network_not_found, no_link), `join_limit_s`. `PUT` replaces the whole `wifi` section: 202, 400 with `error` (an SSID is required), 409 while another change saves |
| `POST /api/wifi/scan` / `GET /api/wifi/scan/results` | WiFi scan for the setup page: `state`, `networks` (one per SSID, strongest first, hidden left out, at most `WifiScanList::kMaxNetworks`) |
| `GET /api/config` | the running configuration without the WiFi password. LAN only |
| `GET /api/config/export`, `POST /api/config/import` | the whole configuration, WiFi password included, as the download `<hostname>-config.json`. Import: JSON (415 otherwise), at most `ConfigStore::kMaxBytes`; `schema` required, keys left out keep their values, `rules` replaces every rule; 202, 400 with `error`, 409 while another change saves; saved, then a restart after `RestartService::kDelayMs`. LAN only |
| `PUT /api/config/timezone` | `{"timezone": "Europe/Berlin"}`; 202, 400 when unknown, 409, 415; saved and in use at once. LAN only |
| `GET /api/config/device`, `PUT /api/config/device` | `GET` `{log_level, heartbeat_s, battery_scale_ppm, battery_mv, levels, limits: {heartbeat_s}}`; `PUT` `{log_level, heartbeat_s}`, a key left out keeps its value, `hostname`, `timezone`, `use_random_ble_address` and `battery_scale_ppm` refused (400); 202, 400, 409, 415; the level in use once saved. LAN only |
| `PUT /api/config/battery` | `{"millivolts": N}`, the battery's actual voltage: the scale that makes the uncalibrated reading N, so a second calibration replaces the first; 0 sets it back to none. 202, 400 (also beyond `kMaxBatteryCorrectionPpm`), 409 while the relay reads no battery or another change saves, 415; in use from the next reading once saved. LAN only |
| `GET /api/config/save` | `{save}` of the last import, time zone or rule change: idle, pending, saved, failed. LAN only |
| `GET /api/timezones` | `{zones}`, every zone name, sorted. LAN only |
| `POST /api/ota` / `GET /api/ota/progress` | multipart firmware upload into the other slot, optional `?md5=`; 200 `{slot, written, restart_in_ms}`, then a restart after `OtaService::kRestartDelayMs`; 400 with `error` (not an image, failed verification, running image not yet confirmed, connection lost); 409 while another upload runs. Progress `{state, written, error?}`: idle, receiving, verified, failed. LAN only |
| `POST /api/reboot` | 202 `{restart_in_ms}`, a restart after `RestartService::kDelayMs`. LAN only |

## 11. mDNS, WiFi, time, log and OTA

**mDNS** (`MdnsService`): `<hostname>.local` and `_http._tcp` on the web
port, nothing else. The ESP-IDF mdns component follows the station and AP
interfaces itself. A hostname change applies at the next boot.

**WiFi** (`lib/wifi/WifiSupervisor` with `Esp32WifiRadio`): station with the
saved credentials; the ESP-IDF WiFi settings stay in RAM
(`WiFi.persistent(false)`), `config.json` is the only store; the core's own
reconnect is off. 2.4 GHz only; static address optional; the hostname goes
to the DHCP server.

- **Attempts**: `WifiSupervisor::kAttemptMs` (15 s) each, the waits between
  them from `kBackoffMs` (5, 10, 20, 30, then 60 s, the last repeating). The
  station scans all channels and joins the strongest access point with the
  name (`WIFI_ALL_CHANNEL_SCAN`, by signal).
- **Setup portal**: the open AP with a DNS catch-all; every unknown URL
  redirects to the setup page, which makes the phones' connectivity checks
  open it. It opens `kFallbackMs` after the link is lost or a boot without
  one, `kJoinLimitMs` after a Connect. A client joining the AP ends the
  attempt in progress (joining would move the radio, and the AP with it, to
  the network's channel); the waits count again once the last client left.
  Connect keeps everything for `kHandoverMs` (2 s), so the page reads
  "saved", then drops the old link or the AP. In the join after a Connect,
  reasons 202 AUTH_FAIL, 15 4WAY_HANDSHAKE_TIMEOUT and 204 HANDSHAKE_TIMEOUT
  count as a rejected password, 201 NO_AP_FOUND, 210 and 211 as not found; 8
  ASSOC_LEAVE, the relay's own disconnect, is ignored. Once the station has
  an address the AP closes.
- **Roaming**: while connected the signal is sampled every `kSignalSampleMs`
  (1 s), the last sample kept for the status (section 13); when its mean
  over a window of `kSignalWindowMs` (30 s) is below `kRoamBelowDbm`
  (-70 dBm), a scan for the network's name only (active,
  `Esp32WifiRadio::kScanMsPerChannel` per channel, the link staying up, at
  most `kRoamScanLimitMs`) looks for an access point at least `kRoamMarginDb`
  (8 dB) stronger and joins it by BSSID and channel. After a scan, found or
  not, the window is watched again `kRoamRestMs` (5 min) later. A roam drops
  the link for a few seconds; TCP clients reconnect. The setup page's scan
  and the roaming scan take turns.
- **LED**: portal 1 Hz, connecting 5 Hz, connected a short blink every 3 s.

**Log**: levels error, warn, info, debug; lines
`[25/12/2026 15:38:42.789] I tag: message`, at most `Logger::kMaxLine`
characters, to Serial (115200, a transmit buffer of `SERIAL_TX_BUFFER_BYTES`:
a line returns once its bytes are in it) and the RAM ring served by
`/api/log`. Before the clock is set the stamp is `[   12345 ms]` since boot
(a software restart keeps the RTC time).

**Time**: SNTP (`lib/esp32/TimeSync`, `esp_netif_sntp`) from `NTP_SERVER_1`
and `NTP_SERVER_2` (`pool.ntp.org`, `time.cloudflare.com`, neither smearing
leap seconds), every 3 h; the clock counts as set once past
`TimeSync::kClockSetAfter`; local time of `device.timezone`. The zones and
their POSIX rules are `lib/time/TimeZoneData.cpp`, from tzdata, kept as it
is: the zones of `zone.tab` plus UTC.

**Core logs**: `lib/esp32/IdfLogBridge` hooks `esp_log_set_vprintf`, splits
each ESP-IDF line into level, tag and message and logs it again; Arduino's
`log_x` takes the same path under the tag `arduino`. ROM and panic output
stays on the UART.

**Hexdump**: per rule, every TCP to BLE packet and BLE to TCP frame with
direction and length, the first `kHexdumpBytes` of each, `kHexdumpLineBytes`
a line.

**Heartbeat** (`Heartbeat`, debug, every `device.heartbeat_s`, 0 none,
counted in `HEARTBEAT_MS` ticks), one line: the heap (free, lowest, largest
block), for each scheduler's thread by its core the most stack it has used
and its size, the log ring's bytes and dropped lines.

**Profiling**: SimpleProfiler's scoped counters (`NAME_SCOPED_COUNTER`,
microseconds with `ScopedCounter<true>`), compiled in with
`ENABLE_PROFILE=1` and printing through `printf`; none stays in the code.

**OTA** (`lib/web/OtaService`): one multipart `POST /api/ota`, written into
the other slot by `Update` as it arrives (the web server hands it over in
pieces of `RESPONSE_STREAM_BUFFER_SIZE`, 1460 B; TCP lets at most its receive
window wait). The writes run in the web server's task, the one handler that
blocks; the upload connection's receive timeout is `OtaService::kRxTimeoutS`
instead of the server's 3 s. `Update` checks the image magic on the first
block; `esp_ota_set_boot_partition()` verifies the whole image before
switching. An update is refused while the running image waits for its
confirmation. Rollback: the firmware's `verifyRollbackLater()` returns true,
so the core does not confirm a new image before `setup()`; `OtaService`
confirms it after `kConfirmAfterMs` (30 s) on the application thread, and a
reset before that boots the previous slot (checked on the board with a
crashing image). The boot log and `/api/status` show both slots; their states
are read from the OTA data at boot, after a confirm and after a verified
upload (`OtaService::readStates()`), the slots themselves once (`BoardInfo`).

## 12. Safety rules

- The relay never originates traffic to a device. It writes only bytes a TCP
  client sent; the probe reads and writes nothing.
- Management only over HTTP; no command channel on the data ports.
- BLE only towards the devices: no other protocol to them.

## 13. Platform and library facts

Found in the libraries' sources and on the board; the code depends on them.

Toolchain and core:

- pioarduino's `builder/penv_setup.py` installs `pioarduino` (its fork of
  PlatformIO Core) and about fifteen Python tools into `<core_dir>/penv` on
  every build, with no switch to turn it off: hence the per-project
  `core_dir`. Never run two `pio` processes during a first install (a locked
  DLL breaks it). `.pio-core/penv` is uv-managed, without pip.
- `lib/` does not see `include/BuildConfig.h`: a value both need is a `-D`
  in `platformio.ini`'s `[common]`.
- Arduino's `log_x` warnings and info are compiled out unless
  `LOG_LOCAL_LEVEL` is set: most core files include ESP-IDF headers first,
  which set it to the maximum level of the prebuilt sdkconfig (error). The
  prebuilt ESP-IDF also filters at run time with default level error; the log
  bridge raises the `arduino` tag.
- `WiFi.config()` swaps its arguments when the mask does not start with 255,
  hence `WiFi.STA.config()`. The core's default fast scan joins the first
  access point with the name from channel 1 up. On the first failure after
  boot the core retries once by itself. `WiFi.mode()` from AP+STA to STA
  keeps the station link.
- ESPAsyncWebServer: every connection gets `setRxTimeout(3)`, cleared only
  when it answers. A plain response sends from its own `String`, a copy of
  the body; every response holds a send buffer of 2 x MSS (2872 B) while it
  goes out, and `request->send()` hands the first segment to lwIP's task in
  two synchronous calls. A JSON answer up to `WebComponent::kMaxTextJson`
  (4 KB) is serialized in one call into a buffer of its exact size, allocated
  without throwing (none: the connection is closed, no answer); a longer one,
  or any while the largest free block is under `kHeapShare` (4) times its
  length, holds its document and is serialized from it piece by piece
  (`WebComponent::makeJsonResponse()`); the log streams from its ring, which
  exists anyway. The JSON handler holds the whole body before parsing
  (`setMaxContentLength`). Its `HTTP_*` names clash with ESP-IDF's
  http_parser enum, hence one handler per method. A plain path matcher also
  matches everything below it; exact matchers are used.
- ArduinoJson copies every `const char*` value and links only string
  literals, so a temporary's `c_str()` is safe in an assignment; its
  formatter writes strings to a `Print` byte by byte.
- lwIP (prebuilt): a socket's receive mailbox holds 6 segments
  (`CONFIG_LWIP_TCP_RECVMBOX_SIZE`), each pinning one of the WiFi driver's 32
  receive buffers (`CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM`) until the
  application reads it, and `SO_RCVBUF` does not apply to TCP: hence the
  relay always reads (section 5). A socket's send queue holds up to
  `TCP_SND_BUF` (5744 B) of unacknowledged data on the heap. `SO_LINGER` is
  compiled out: a closed socket keeps its unacknowledged data until TCP gives
  up (`TCP_MAXRTX` 12, about 40 min), while TCP keepalive
  (`LWIP_TCP_KEEPALIVE`) resets an open connection whose peer stopped
  answering and frees it at once: hence keepalive before the stall closure.
  16 TCP control blocks (`CONFIG_LWIP_MAX_ACTIVE_TCP`) serve every
  connection, the web server's and the lingering ones included.
- Arduino's `Serial` has no transmit buffer unless `setTxBufferSize()`
  precedes `begin()`: a write then blocks until its bytes fit the 128-byte
  FIFO. `WiFi.SSID()`, `RSSI()`, `BSSIDstr()` and `channel()` each fetch the
  access point record through the WiFi task, `WiFi.dnsIP()` goes through
  lwIP's task, `ESP.getChipModel()` and `getChipRevision()` read the efuses
  at every call, `esp_ota_get_state_partition()` reads the OTA data from
  flash and logs an error without it: hence `BoardInfo` (the board's facts
  and the app slots, read once), `WifiRadio::link()` (the link as a snapshot
  the driver's events and the supervisor's signal sample keep),
  `BleConnectionManager::ownAddress()` and `OtaService::readStates()`.
- The code runs from flash through a 32 KB cache; a path run once per
  request pays its code fetch, about 1 us per 32 bytes at 40 MHz DIO and a
  quarter of that at 80 MHz QIO.
- libstdc++ throws `bad_alloc` when an allocation fails and nothing catches
  it: any failed `std::string` or `std::vector` allocation aborts the
  firmware. The relay's own allocations for a client and for an answer use
  `std::nothrow` and refuse instead.

NimBLE (NimBLE-Arduino 2.5.1):

- NimBLE-Arduino hands `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` to the controller
  (the sdkconfig's `CONFIG_BTDM_CTRL_BLE_MAX_CONN` is unused); the
  controller's limit is 9 (`BTDM_CONTROLLER_BLE_MAX_CONN_LIMIT`).
- `CORE_DEBUG_LEVEL` 3 makes NimBLE-Arduino log a line per advertisement,
  hence `CONFIG_NIMBLE_CPP_LOG_LEVEL=2`.
- NimBLE-Arduino sets a random static address at each start and
  `ble_hs_id_infer_auto()` prefers it; the relay sets its own address type
  (`use_random_ble_address`).
- NimBLE-Arduino's `connect()` during a scan stops the scan;
  `NimBLEScan::stop()` calls no `onScanEnd`.
- NimBLE-Arduino's `readValue()` pairs and asks again on a security error,
  hence `ble_gattc_read_long`. A peripheral's Security Request starts
  pairing; NimBLE-Arduino's defaults bond nothing.
- `BLE_GAP_EVENT_CONNECT` for a central comes only after LE Read Remote
  Features; a link that fails before that (HCI 0x3E) gets only
  `BLE_GAP_EVENT_DISCONNECT`. At LE Connection Complete NimBLE resets its
  connect state, so `ble_gap_conn_cancel()` then returns `BLE_HS_EALREADY`.
- When the peer exchanged the MTU first, `ble_gattc_exchange_mtu()` returns
  `BLE_HS_EALREADY`; `ble_att_mtu()` has the MTU, and `BLE_GAP_EVENT_MTU`
  comes whichever side asked.

## 14. Memory and measurements

Memory, measured on firmware 0.3.3 with 9 connections: static `.data` and
`.bss` 76 KB; the 8-bit heap 207 KB before `setup()`, of which WiFi takes
52 KB, mDNS 5.6 KB, the web routes 3.7 KB, NimBLE with the controller 44.5 KB
(the controller keeps 40.7 KB more at fixed addresses and releases 15.5 KB of
classic-BT memory; the memory of all 9 links is reserved here, a link costs
nothing more when it connects), the relay thread 6.8 KB and the web server
10 KB (its task `CONFIG_ASYNC_TCP_STACK_SIZE`); 58 to 63 KB free at rest,
connected. A rule costs its RX ring, its server and its config copies, about
1.5 KB; a TCP client its buffer and its socket, 1.5 KB idle and about 3.6 KB
under traffic with its replies in flight, up to 5.7 KB more while it stops
acknowledging, until the keepalive resets it; a status answer about 5 KB
while it is built and sent, 3.5 ms of the web server's task at 80 MHz QIO
(5 to 6 ms at 40 MHz DIO). Vectors keep their storage between uses (the
rules' statuses, a connection's discovery), a few KB after a run with rules.
The page holds one request in flight.

Load, from the stress test with 200 B requests and 480 B replies every
second per client: one link carries 11 heavy clients without a restart and 4
without a lost reply; the link saturates at 3, from there replies queue. The
twelfth client leaves the web server no buffer for an answer, which it cuts.

## 15. The laptop as a GATT server

The end-to-end tests (`test/fixtures`) use a Windows laptop as the BLE
peripheral, a `bless` GATT server with its own service
(`7d2a1000-4c1e-4b8a-9a55-2f1e6b7c0d00`: write with response + indicate,
write without response + notify). Framed requests and replies (`A5`,
sequence, lengths, payload made from the sequence; `5A` for a reply) let the
server check every request whatever writes it came in, and the client every
reply whatever indications carried it. What bless 0.3.0 on Windows does:

- It installs only on Python 3.11 (its WinRT pins conflict with every bleak
  it accepts on 3.12 and later) and imports `pysetupdi`, which it does not
  list.
- The advertised name is the adapter's, the computer's name; bless sets
  another only by renaming the adapter (`name_overwrite`), never used.
- Windows answers the discovery and the GAP and Device Information reads
  itself; bless sees none of them. It exchanges the MTU first (255) and
  advertises with a random resolvable address.
- A long write reaches bless as two writes, in order. Two writes without
  response 1 to 2 ms apart can reach the script swapped (bless handles each
  on the WinRT thread that raised it); the burst scenario puts them back in
  the order of the bytes it sent and says how many came early.
- WinRT gives a GATT server no call to drop a central's link, and bless's
  `stop()` only stops advertising, so a lost link has no scenario. A write
  the script holds unanswered gets Windows' own answer after about 20 s, ATT
  0x0E (Unlikely Error), before NimBLE's 30 s request timeout would end the
  link; bless's late answer then fails ("The object has been committed").

## 16. Not built yet

- The web server's connections with keepalive.
- A watchdog.
- A config schema migration hook.
- The LED's BLE and traffic patterns: `LedIndicator` has a solid pattern for
  a BLE link and a `kFlickerMs` inverted flicker for traffic; nothing sets
  them.
- A log persistence option.
- The schedulers' stack figures in `/api/status` (the heartbeat logs them).
- A 24 h soak against the laptop's GATT server: no reboot, no heap decline.
- An optional client-IP allowlist on the data ports.

## 17. Code map

| Where | What |
|---|---|
| `lib/core` | `Component`, `DigitalOut`, `Lock`, `FileSystem`, `TaskList`, `FixedString` |
| `lib/log` | `LogRing`, `Logger`, `LogStamp`, `IdfLogLine` |
| `lib/json`, `lib/config` | serialization; `Config`, `ConfigData`, `ConfigStore`, `ConfigRules`, `DeviceConfig`, `WifiConfig`, `StaticIpConfig`, `IPv4Str`, `RuleConfig`, `TargetConfig`, `BleChannelConfig` |
| `lib/wifi`, `lib/time`, `lib/led` | `WifiSupervisor`, `WifiRadio`, `WifiLink`, `WifiScanList`, `AccessPoint`, `BSSIDStr`; `TimeZones`; `LedIndicator` |
| `lib/ble` | `BleAddress`, `BleUuid`, `BleDeviceBase`, `BleDevice`, `BleScanList`, `GattTable`, `HexText` |
| `lib/relay` | `DeviceRules`, `ChannelHandles`, `PacketRing`, `TcpClientData`, `WriteMode`, `RelayStatus` |
| `lib/esp32` | the glue: `Scheduler`, `Thread`, `FreeRtosMutex`, `BoardInfo`, `Esp32WifiRadio`, `Esp32DigitalOut`, `TimeSync`, `MdnsService`, `IdfLogBridge`, `Heartbeat`, `BatteryMonitor`, `LittleFsStorage`, `BleScanner`, `BleDiscovery`, `BleConnection`, `BleConnectionManager`, `TcpServer`, `NimBleConvert` |
| `lib/web` | every HTTP module, each a `WebComponent`: `WebPages`, `SetupPortal`, `StatusService`, `LogService`, `ConfigService`, `RuleService`, `BleService`, `OtaService`, `RestartService` |
| `test/test_*` | host unit tests (`pio test -e native`); `test/fixtures` the integration and stress tests |
