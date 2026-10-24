# ble-relay: build, test and layout notes

Claude builds and runs the host unit tests; the user flashes, runs the
fixtures and pastes output back. Claude never talks to a running relay
unless asked to.

The design, the configuration and relay semantics, the HTTP API and the
platform facts the code depends on: `docs/design.md`. Read it before
changing behaviour; keep it true when behaviour changes.

## Tooling
- PlatformIO Core CLI: `pio`; on Windows, when it is not on PATH,
  `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.
- Platform `espressif32 55.03.312` (pioarduino: Arduino core 3.3.12 on
  ESP-IDF 5.5.5), pinned by release URL in `platformio.ini`.
- `core_dir` is per project: `${PROJECT_DIR}/.pio-core`.
  Platforms, packages, toolchain and the pioarduino platform's own Python
  environment (`.pio-core/penv`, holding the `pioarduino` core fork and the
  esptool family) live there, git-ignored. The `pio` executable and the
  official core stay in `~/.platformio/penv`. The first build downloads
  about 1 GB; never run two `pio` processes during that first install.
  `.pio-core/penv` has no pip (uv-managed); list it with
  `.pio-core\penv\Scripts\uv.exe pip list --python .pio-core\penv\Scripts\python.exe`.
- Host compiler for the `native` env: a `g++` on PATH (on Windows, MSYS2
  UCRT64 GCC); PlatformIO calls plain `g++`.

## Commands Claude may run
```
pio run                     # builds env TZT-ESP32-Lite and env native
pio run -e TZT-ESP32-Lite   # firmware only
pio test -e native     # host unit tests
```

## Commands the user runs (flash, serial)
```
pio run -e TZT-ESP32-Lite -t upload --upload-port <port>
pio device monitor -p <port> -b 115200
```
`default_envs` lists both envs, so always pass `-e TZT-ESP32-Lite` to upload.

Firmware update over the network, from the Firmware view at
http://w2b.local/ or with curl:
```
curl -F "firmware=@.pio/build/TZT-ESP32-Lite/firmware.bin" http://w2b.local/api/ota
```
Config, from the page's Config view or with curl. The export holds the
WiFi password in plain text; the import writes `/config.json` and restarts
the relay:
```
curl -OJ http://w2b.local/api/config/export
curl -H "Content-Type: application/json" --data-binary @w2b-config.json http://w2b.local/api/config/import
curl "http://w2b.local/api/log?since=0"
```
Relay rules, from the page's Relays view or with curl; each change writes
`/config.json`. An enabled rule listens on its port; with policy always it
connects to its device and subscribes its RX, with on demand once a TCP client
connects, until `idle_s` after the last one left; disabling drops its clients
and its link at once. `/api/status` lists every rule's link, clients and
counters under `relays`. The
relay gives a new rule the first free id of `id0`, `id1`, ...; a rule's export
is a valid POST body, its id not used:
```
curl http://w2b.local/api/rules
curl -OJ http://w2b.local/api/rules/id0/export
curl -H "Content-Type: application/json" --data-binary @w2b-rule-id0.json http://w2b.local/api/rules
curl -X PUT -H "Content-Type: application/json" --data-binary @w2b-rule-id0.json http://w2b.local/api/rules/id0
curl -X DELETE http://w2b.local/api/rules/id0
```
BLE scan, from the form's Scan button or with curl; `{}` scans
`BleScanner::kDefaultSeconds`, `{"seconds": N}` up to `kMaxSeconds`, and the
answer says how long. The relay sends scan requests to every device in
range. Once the page has the list, it
calls clear, which frees the relay's copy (the page keeps its own):
```
curl -H "Content-Type: application/json" -d "{}" http://w2b.local/api/ble/scan
curl http://w2b.local/api/ble/scan/results
curl -X POST http://w2b.local/api/ble/scan/clear
```
BLE probe, from a scan row's Probe button, a pick, or with curl: the
request carries the device's `address`, `address_type` and `connectable` as
the scan listed them (not connectable: 400, no connect); the relay connects,
reads its GATT table with
discovery requests, then the values of GAP's name and appearance and of
Device Information's characteristics that offer read (never pairing for
one), writes nothing and disconnects; the device's link is taken meanwhile,
and a device that asks for security gets Just Works pairing without a bond.
The scan and the probe take turns (409):
```
curl -H "Content-Type: application/json" -d "{\"address\": \"xx:xx:xx:xx:xx:xx\", \"address_type\": \"public\", \"connectable\": true}" http://w2b.local/api/ble/discover
curl http://w2b.local/api/ble/discover/result
```
TCP relay: a client connected to a rule's port gets every indication of
its RX, and its bytes go to its TX. Bytes beyond its 1 KB buffer are dropped
and counted; a client that stops answering is reset by keepalive within 11 s,
one that does not read is closed after 15 s.

The integration test (`test/fixtures/integration_test.py`, no options) runs
the relay end to end from the laptop: it advertises its own GATT service
(`7d2a1000-...`, two write/indicate pairs, nothing of the HF-LPT270's), finds
the laptop in the relay's scan (scan requests go to every device in range),
makes its rules `w2b-test A` and `B` through the
API on ports from 18901 (each change writes `/config.json`; removed at the
end, and at the next start after a stopped run), then sends requests over TCP
that the GATT server checks and answers, and checks the replies. Eight
scenarios: a request before the link is up and sizes up to MTU - 3; a burst
without response (bless on Windows may pass on two of its writes swapped; the
test puts them back in the order sent and says how many came early); chunk;
long write and drop beyond 512 B; two clients and a refused third; two rules
on one device; on-demand idle disconnect and back; policy always and disable.
It prints PASS or FAIL for each, a failure with the
rule's status and the relay's log. It stops at once when `w2b.local` does not
resolve, and when an enabled rule of yours targets the laptop too (it would
share the link). Exit code 0 all passed, 1 some failed, 2 stopped.

On Windows it needs Python 3.11: bless 0.3.0 does not install on 3.12 and
later (its WinRT pins conflict with bleak's). uv, in the pioarduino penv,
makes the venv and fetches a 3.11 when none is on the machine; `--seed` puts
pip in it. bless also needs `pysetupdi`, which it does not list;
requirements.txt takes it from GitHub, so git must be on PATH. Once, then run:
```
.pio-core\penv\Scripts\uv.exe venv --seed --python 3.11 test\fixtures\.venv
.pio-core\penv\Scripts\uv.exe pip install --python test\fixtures\.venv\Scripts\python.exe -r test\fixtures\requirements.txt
test\fixtures\.venv\Scripts\python.exe test\fixtures\integration_test.py
```

The stress test (`test/fixtures/stress_test.py`, same venv; `--heavy` for 200 B
requests and 480 B replies every second instead of 24 B and 48 B every 2 s; `--rules`
for a ladder of rules with one client each, the memory shape of that many BLE
devices, since a link's memory is reserved at boot and a rule's and a client's are
not) finds how
much the relay takes: this computer's GATT server is one BLE device, and every
other device in range advertising the test service joins. It climbs a ladder of
k TCP clients per device, k from 1 up, over as many rules per device as the
clients need (the relay's `limits.clients` per rule, `limits.enabled` rules in
all; rules `w2b-stress d.i`, ports from 18951, one block of 8 per device). Each
step sends regular traffic for `STEP_S` from every client, a request every
`REQUEST_PERIOD_S`, while the page's polling is imitated, and reads the heap,
the lost replies and the relay's drop counters. It stops at the first restart
(the uptime went back; the reset reason from the log) or when the relay stops
answering the page's requests, waits for it to come back and removes its rules
first thing; a failed step prints the failed requests with their error and the
relay's last 20 log lines. The report says how many clients per device held
without a restart and how many without a lost reply or a dropped frame; lost
replies without relay drops are the BLE link's. Exit code 0 report printed,
2 stopped.
```
test\fixtures\.venv\Scripts\python.exe test\fixtures\stress_test.py
```

## Layout rules
- `src/main.cpp` is the only firmware entry point; `src/host/` is host-only.
- Platform-independent code goes in `lib/<module>/` so both the firmware and
  the native tests can include it. Tests go in `test/test_<module>/`;
  `test/fixtures/` holds the fixtures the user runs on the laptop.
- `lib/core/` holds the interfaces (`Component`, `DigitalOut`, `Lock`, `FileSystem`);
  `lib/esp32/` holds the Arduino/FreeRTOS glue and is never included by a
  test. `lib/web/` holds everything that serves HTTP (pages and API), also
  firmware only: each module is a `WebComponent` (a `Component` with the web
  server, `addRoutes()` called by `setup()`, `sendJson` / `sendError`,
  `refuseOnAccessPoint(request)` first in the handler of a LAN-only path).
- `Config` (`lib/config`) is the one running configuration every module
  holds, with its lock inside (user, 2026-09-26): reads return copies or run
  under its lock (`read(reader)`); a change is `submit(what, edit, followUp)`,
  the edit working in place on its staging copy, and its `update()` on the
  application thread saves it to `/config.json`, publishes it and runs the
  follow-up. Its content is the value class `ConfigData`. A web module whose
  requests change it derives from `ConfigWebComponent`. Compile-time defaults (`#ifndef X #define X`) live in
  `include/BuildConfig.h`, which `lib/` cannot include; a value `lib/` needs too is a `-D` in
  `platformio.ini`'s `[common]` build_flags, which both envs see (`HTTP_PORT`,
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`), and the `lib/` constant derives from it
  (`RuleConfig::kWebPort`, `ConfigData::kMaxActiveRules`); hardware facts are `-D` flags in the
  board's env.
- Modules expose `setup()` + `update(dtMs)` (`Component`). A `Scheduler`
  per core owns one FreeRTOS thread and runs *tasks*, plain callables
  registered with `every(periodMs, task)` / `once(delayMs, task)`; wrap a
  module call in a lambda, there are no Component overloads. The thread
  sleeps until the next task is due. Blocking work gets its own
  `Thread::start(body)`. Arduino's `loop()` is not used. Modules shared
  between threads take a `Lock`.
- One PlatformIO environment per hardware, named after the board
  (`TZT-ESP32-Lite`), plus `native`.
- Web pages are files in `web/`, linked into the firmware image, so they
  flash and update with the code. Never on LittleFS. The
  pre-build script `scripts/web_assets.py` inlines `<link rel="stylesheet"
  href="x.css">` and `<script src="x.js"></script>` from `web/`, minifies by line and gzips every `web/*.html`
  into `.pio/build/<env>/generated/WebAssets.h` (array `indexHtml` for
  `index.html`); `lib/web/WebPages` serves them. The minify only strips
  whitespace, comments and `//` comment lines, so pages hold no multi-line
  template literals and no static `<pre>` text. A page opens from disk in a
  browser for layout work; its API calls fail there.

## Coding style (agreed with the user 2026-09-23)
- Opening brace `{` on its own line, everywhere: functions, `if`/`else`,
  loops, `switch`, lambdas, classes, namespaces (Allman style).
- One source per value (user, 2026-09-27): a limit, default or timing is defined once, and
  everything else derives from it. Code uses the constant; the pages take it from the API
  (`GET /api/rules` gives a new rule's `defaults` and the fields' `limits`); comments and docs
  name the constant instead of repeating its number.
- Every `if`, `else`, `for`, `while` and `do` body is braced, one-liners too.
- Every `case` body is braced too, the braces on their own lines under the
  label, its `break` or `return` inside; labels that share a body stand one
  above the other over its one block (user, 2026-09-27). The exception: a
  body that is a single `return` stays on its label's line, the returns
  aligned, as in `lib/esp32/BoardInfo.cpp`.
- One class, one header and one source: `X.h` declares it, `X.cpp` holds all
  of its implementation; never spread a class over more files (user,
  2026-09-29). Small helper types may share their class's pair.
- Blank lines separate the logical blocks inside a function. Functions stay
  short: the meaningful body fits on one screen, about 40 lines. Split longer
  ones into small helpers; `static` or `inline` helpers in the same file are
  fine when they serve a single caller.
- Whitespace as `.editorconfig` says: a tab per indent level, LF line ends,
  UTF-8, no trailing whitespace, a final newline; Python, INI, JSON and
  Markdown keep spaces. `camelCase` functions and variables, `UPPER_CASE`
  macros.
- Names and comments use plain, common English words (user, 2026-09-28;
  the user is not a native speaker): `updateTimings()`, not `age()`;
  `startSearch()`, not `listen()`. Method names are verbs that say what the
  method does; a getter may drop "get" (`seconds()`, `currentState()`);
  callbacks are `onEvent` (`onConnected()`, `onMtu()`). No class is named
  as the plural of another: `BleConnectionManager` holds `BleConnection`s.
- Line width: up to 160 characters, 180 at most.
- Wrapped lines (arguments, parameters, conditions, a chained call) go one
  indent step deeper than the line they continue. Never align
  them with an opening parenthesis or with the arguments above (user,
  2026-09-25).
- A lambda's braces go on their own lines, even for a single statement, one
  step deeper than the line that holds its `[...](...)`. As the first or
  second argument of a call that stays short, the `[...](...)` stays on the
  call's line (user, 2026-09-25); further back it starts a line of its own,
  one step in:
  ```
  application
    .every(HEARTBEAT_MS, [](uint32_t dtMs)
      {
        heartbeat.update(dtMs);
      })
    .start();

  server()
    .on(AsyncURIMatcher::exact("/api/config/import"), HTTP_POST,
      [this](AsyncWebServerRequest* request, JsonVariant& json)
      {
        handleImport(request, json);
      })
    .setMaxContentLength(ConfigStore::kMaxBytes);
  ```
- Class layout, top to bottom: constants and `constexpr`; nested types and
  aliases (public ones here; a private nested type goes right before its
  first use); constructors and destructor; methods, `public` then
  `protected` then `private`, the inline getters of each section as one
  group first; fields, `public` then `protected` then `private`.
- Comments (user, 2026-09-26; the repository goes public): they state what
  is. Never history ("was", "no longer", dates), decisions or who
  asked for something, never a plan document, its sections or its decision
  and phase numbers. In a header a comment is at most 2 lines, the file
  comment included; in a `.cpp`, `.html`, `.css`, `.py` or `.ini` at most 1
  line, and only where the code is not obvious or to set a block apart.
  A state change is named in it too ("Writes /config.json.").
- Every public class, enum, enum value, free function and public member
  (method, constructor, constant, alias) has a comment; destructors and
  deleted members need none. Private members get one only where not
  obvious. Comments go on their own line above the member, never on its
  right. A class's comment sits directly above it; a header that holds one
  class has no file comment of its own and starts with `#pragma once`.
- One blank line between every two members, fields included.
- `const` getters that return a field, and operators whose body is one
  statement, are one-liners in the header with an explicit `inline`, the one
  exception to the brace rule (user, 2026-09-26):
  `inline const SSID& ssid() const { return network; }`.
- An aggregate (a struct without constructors, a C struct too) is initialized with designated
  initializers, never by position (user, 2026-09-29); both envs compile as gnu++2b. Up to 2
  fields stay on one line, `Search{.state = SearchState::Searching, .target = target}`; more go
  one a line under the type, its braces on their own lines; as an argument the type starts a line
  one step in, or it goes into a variable first:
  ```
  auto entry = Entry
  {
    .lastMs = clock(),
    .delayMs = delayMs,
    .repeat = repeat,
    .task = std::move(task)
  };

  self->services.push_back(
    Service
    {
      .uuid = toBleUuid(NimBLEUUID(service->uuid)),
      .start = service->start_handle,
      .end = service->end_handle
    });
  ```
  A class with a constructor keeps it, called by its name, never built implicitly by `emplace`
  or a braced list: `status = RelayStatus(id, name, ...)`, `found.push_back(AccessPoint(bssid, rssi, channel))`.
- Field names carry no `m_` prefix and no `_` suffix. A field never shares
  a name with a method, so a getter `size()` reads a field named `used`.
- State is read through `const` getters, never public fields. Prefer
  methods that do not modify state, compute instead of caching, and keep
  as little state as possible.
- Data classes: private fields, `const` getters, public
  constructors that set every field. Text of a known limit is a
  `FixedString<N>` (`lib/core`) under a named alias (`WifiConfig::SSID`,
  `IPv4Str`), next to the function that parses or checks it; a list of a
  known limit is a `std::array` (user, 2026-09-26); addresses and UUIDs are
  bytes. A list with no fixed limit, the rules, is a `std::vector`.
  `ConfigData`, the whole file, lives in `Config` and is edited in place. When the default state is valid, the
  default constructor is public too and `fromJson` keeps the current value
  of a key the JSON leaves out. When it is not, the default constructor is
  private, declared next to `friend class SerializableFactory;`, used only
  through `SerializableFactory::createEmpty<T>()`, and the keys are required.
- Persisted classes implement `IJsonSerializable<TSelf>` (`lib/json`):
  `toJson(JsonObject&) const` and `fromJson(const JsonObjectConst&,
  std::string& error)`, each at the class's own nesting level; `fromJson`
  ends with `validate(error)`. Headers include `JsonSerializable.h` only
  (ArduinoJson forward-declared); `.cpp` files include `JsonMembers.h`.
