# The relay's integration test: a GATT server on this computer, rules made through w2b.local's web API, TCP clients through them.
import asyncio
import dataclasses
import json
import math
import platform
import socket
import sys
import time
import urllib.error
import urllib.request

HOST = "w2b.local"

HTTP_PORT = 80

# the test's own GATT service and characteristics, none of the HF-LPT270's
SERVICE = "7d2a1000-4c1e-4b8a-9a55-2f1e6b7c0d00"
TX1 = "7d2a1001-4c1e-4b8a-9a55-2f1e6b7c0d00"
RX1 = "7d2a1002-4c1e-4b8a-9a55-2f1e6b7c0d00"
TX2 = "7d2a1003-4c1e-4b8a-9a55-2f1e6b7c0d00"
RX2 = "7d2a1004-4c1e-4b8a-9a55-2f1e6b7c0d00"

# the test's rules are named so; one left by a stopped run is removed at the start
RULE_NAME = "w2b-test"

FIRST_PORT = 18901

SCAN_S = 8

HTTP_TIMEOUT_S = 5

CONNECT_S = 5

# the first reply waits for the relay's search, the connect and the discovery; a request waits in the relay up to 10 s
FIRST_REPLY_S = 20

REPLY_S = 5

# how long "nothing arrives" is watched
QUIET_S = 2

# a link through a few failed attempts and their backoff
STATE_S = 40

# the longest ATT value; a long write takes no more
MAX_ATTRIBUTE_VALUE = 512

# a request: A5, sequence number, payload length (2 bytes LE), reply length (2 bytes LE), payload
REQUEST = 0xA5

REQUEST_HEADER = 6

# a reply: 5A, sequence number, payload length (2 bytes LE), payload
REPLY = 0x5A

REPLY_HEADER = 4


@dataclasses.dataclass(frozen=True)
class Pair:
    tx: str
    rx: str
    with_response: bool
    subscribe: str


CONFIRMED = Pair(TX1, RX1, True, "indicate")

UNCONFIRMED = Pair(TX2, RX2, False, "notify")

ANSWER_ON = {TX1: RX1, TX2: RX2}


class Failure(Exception):
    pass


class SetupError(Exception):
    pass


# the relay did not answer HTTP: every test after it would fail too
class RelayGone(Exception):
    pass


def short(uuid):
    return uuid[4:8]


def request_payload(seq, size):
    return bytes((seq * 7 + i * 13 + 1) & 0xFF for i in range(size))


def reply_payload(seq, size):
    return bytes((seq * 11 + i * 3 + 0x80) & 0xFF for i in range(size))


# a request of total bytes, header included, asking for a reply of reply_total bytes
def make_request(seq, total, reply_total):
    size = total - REQUEST_HEADER
    reply_size = reply_total - REPLY_HEADER

    return bytes([REQUEST, seq]) + size.to_bytes(2, "little") + reply_size.to_bytes(2, "little") + request_payload(seq, size)


def make_reply(seq, total):
    size = total - REPLY_HEADER

    return bytes([REPLY, seq]) + size.to_bytes(2, "little") + reply_payload(seq, size)


@dataclasses.dataclass
class Received:
    seq: int
    uuid: str
    total: int
    # the sizes of the BLE writes that carried it
    writes: list
    error: str = ""


# One TX characteristic's bytes: whole requests out of any split of them into writes.
class Inbox:
    def __init__(self, uuid):
        self.uuid = uuid
        self.data = bytearray()
        # per byte of data, the index of the write it came in
        self.origin = []
        self.write_sizes = []
        # the bytes expected next, from position on; a write that comes before an earlier one waits in held, by its offset
        self.expected = b""
        self.position = 0
        self.held = {}
        self.early = 0

    # The bytes the next writes carry: bless on Windows may pass on two writes without response swapped; they are put back in order.
    def expect(self, stream):
        self.expected = stream
        self.position = 0
        self.held = {}
        self.early = 0

    # the requests this write completes, each with the reply length it asks for
    def add(self, chunk):
        self.write_sizes.append(len(chunk))
        found = []

        for index, piece in self.put_in_order(len(self.write_sizes) - 1, chunk):
            self.data += piece
            self.origin += [index] * len(piece)
            found += self.parse()

        return found

    # this write and the held ones it lets go, in the expected order; a write the expected bytes do not hold passes as it came
    def put_in_order(self, index, chunk):
        offset = self.find(chunk) if self.position < len(self.expected) else None

        if offset is None:
            return [(index, chunk)]

        if offset > self.position:
            self.held[offset] = (index, chunk)
            self.early += 1
            return []

        ready = [(index, chunk)]
        self.position += len(chunk)

        while self.position in self.held:
            index, chunk = self.held.pop(self.position)
            ready.append((index, chunk))
            self.position += len(chunk)

        return ready

    # where chunk lies in the expected bytes, from position on and clear of the held writes; None when nowhere
    def find(self, chunk):
        start = self.position

        while True:
            offset = self.expected.find(chunk, start)

            if offset < 0:
                return None

            if not any(offset < at + len(piece) and at < offset + len(chunk) for at, (_, piece) in self.held.items()):
                return offset

            start = offset + 1

    def parse(self):
        found = []

        while self.data:
            if self.data[0] != REQUEST:
                start = self.data.find(bytes([REQUEST]))
                cut = len(self.data) if start < 0 else start
                found.append((Received(-1, self.uuid, cut, [], f"{cut} bytes that start no request: {bytes(self.data[:16]).hex(' ')}"), 0))
                self.drop(cut)
                continue

            if len(self.data) < REQUEST_HEADER:
                break

            size = int.from_bytes(self.data[2:4], "little")
            total = REQUEST_HEADER + size

            if len(self.data) < total:
                break

            seq = self.data[1]
            reply_total = REPLY_HEADER + int.from_bytes(self.data[4:6], "little")
            writes = [self.write_sizes[i] for i in sorted(set(self.origin[:total]))]
            payload = bytes(self.data[REQUEST_HEADER:total])
            error = "" if payload == request_payload(seq, size) else f"request {seq}: its {size} payload bytes are not the ones sent"
            found.append((Received(seq, self.uuid, total, writes, error), reply_total))
            self.drop(total)

        return found

    def drop(self, count):
        del self.data[:count]
        del self.origin[:count]


# The test's GATT server on this computer (bless): checks each request that arrives on a TX and answers it on the paired RX.
class Peripheral:
    def __init__(self):
        self.loop = None
        self.server = None
        self.data_writer = None
        self.sending = None
        self.inboxes = {}
        self.received = {}
        self.errors = []

    async def start(self):
        try:
            from bless import BlessServer, GATTAttributePermissions, GATTCharacteristicProperties
        except ImportError:
            raise SetupError("bless is missing: run this script with test\\fixtures\\.venv\\Scripts\\python.exe (ble-relay/CLAUDE.md)")

        try:
            from bleak_winrt.windows.storage.streams import DataWriter
        except ImportError:
            from winrt.windows.storage.streams import DataWriter

        self.loop = asyncio.get_running_loop()
        self.data_writer = DataWriter
        self.sending = asyncio.Lock()
        self.clear()
        # name_overwrite stays off: on Windows bless would rename this computer's Bluetooth adapter
        self.server = BlessServer(name=RULE_NAME, loop=self.loop)
        self.server.read_request_func = self.on_read
        self.server.write_request_func = self.on_write
        await self.server.add_new_service(SERVICE)
        permissions = GATTAttributePermissions.readable | GATTAttributePermissions.writeable
        flags = GATTCharacteristicProperties
        characteristics = {TX1: flags.write, RX1: flags.indicate, TX2: flags.write_without_response, RX2: flags.notify}

        for uuid, properties in characteristics.items():
            await self.server.add_new_characteristic(SERVICE, uuid, properties, None, permissions)

        # bless's start() waits on this event without a timeout; the loop below waits with one
        started = getattr(self.server, "_advertising_started", None)

        if started is not None:
            started.set()

        await self.server.start()
        deadline = time.monotonic() + 10

        while not await self.server.is_advertising():
            if time.monotonic() > deadline:
                raise SetupError("advertising did not start within 10 s; is Bluetooth on here?")

            await asyncio.sleep(0.2)

    async def stop(self):
        if self.server is not None:
            await self.server.stop()

    def clear(self):
        self.inboxes = {TX1: Inbox(TX1), TX2: Inbox(TX2)}
        self.received = {}
        self.errors = []

    def on_read(self, characteristic, **kwargs):
        return characteristic.value or bytearray()

    # bless's thread: the bytes go to the event loop
    def on_write(self, characteristic, value, **kwargs):
        self.loop.call_soon_threadsafe(self.take, str(characteristic.uuid).lower(), bytes(value))

    def take(self, uuid, chunk):
        inbox = self.inboxes.get(uuid)

        if inbox is None:
            self.errors.append(f"a write to {short(uuid)}, not a TX of the test")
            return

        for received, reply_total in inbox.add(chunk):
            if received.seq < 0:
                self.errors.append(f"{short(uuid)}: {received.error}")
                continue

            self.received[received.seq] = received

            if not received.error:
                asyncio.ensure_future(self.send(ANSWER_ON[uuid], make_reply(received.seq, reply_total)))

    # Sends frame on rx in pieces the subscriber takes: an indication or notification each, one frame after the other.
    async def send(self, rx, frame):
        async with self.sending:
            characteristic = self.server.get_characteristic(rx)
            piece = await self.piece_size(characteristic)

            if piece == 0:
                self.errors.append(f"nobody subscribed to {short(rx)}: frame {frame[1]} not sent")
                return

            for at in range(0, len(frame), piece):
                await self.notify(rx, characteristic, frame[at:at + piece])

    # the most bytes one indication or notification carries to every subscriber: MTU - 3; 0 when nobody subscribed
    async def piece_size(self, characteristic):
        deadline = time.monotonic() + 3

        while True:
            clients = list(characteristic.obj.subscribed_clients or [])

            if clients or time.monotonic() > deadline:
                return min((client.max_notification_size for client in clients), default=0)

            await asyncio.sleep(0.05)

    async def notify(self, rx, characteristic, piece):
        writer = self.data_writer()
        writer.write_bytes(bytearray(piece))
        results = await characteristic.obj.notify_value_async(writer.detach_buffer())

        for result in results:
            if int(result.status) != 0:
                self.errors.append(f"{short(rx)}: sending {len(piece)} B failed, status {int(result.status)}, ATT error {result.protocol_error}")


# The relay's web API.
class Relay:
    def __init__(self, address):
        self.base = f"http://{address}:{HTTP_PORT}"
        # the log's end at the last log_end(): the next one asks only for what came since
        self.log_next = 0

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"} if data is not None else {}
        request = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)

        try:
            with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_S) as response:
                return response.status, response.read(), response.headers
        except urllib.error.HTTPError as error:
            return error.code, error.read(), error.headers
        except OSError as error:
            raise RelayGone(f"{method} {path}: {error}")

    async def request(self, method, path, body=None, expect=(200,)):
        code, raw, _ = await asyncio.to_thread(self.call, method, path, body)

        try:
            data = json.loads(raw) if raw else {}
        except ValueError:
            data = {"error": raw[:200].decode(errors="replace")}

        if code not in expect:
            raise Failure(f"{method} {path}: HTTP {code}, {data.get('error', data)}")

        return data

    async def status(self):
        return await self.request("GET", "/api/status")

    async def rule_status(self, rule_id):
        relays = (await self.status()).get("relays", [])

        return next((relay for relay in relays if relay.get("id") == rule_id), {})

    async def rules(self):
        return (await self.request("GET", "/api/rules"))["rules"]

    # a new rule's values, as the relay gives them
    async def rule_defaults(self):
        return (await self.request("GET", "/api/rules"))["defaults"]

    # Changes the relay's state: adds a rule, which writes its /config.json; returns the id the relay gave it.
    async def add_rule(self, body):
        await self.request("POST", "/api/rules", body, expect=(202,))
        await self.wait_saved()
        ids = [rule["id"] for rule in await self.rules() if rule["name"] == body["name"]]

        if not ids:
            raise Failure(f"the rule \"{body['name']}\" is not in /api/rules after its POST")

        return ids[-1]

    # Changes the relay's state: replaces a rule, which writes its /config.json.
    async def change_rule(self, rule_id, body):
        await self.request("PUT", f"/api/rules/{rule_id}", body, expect=(202,))
        await self.wait_saved()

    # Changes the relay's state: removes a rule, which writes its /config.json.
    async def remove_rule(self, rule_id):
        await self.request("DELETE", f"/api/rules/{rule_id}", expect=(202,))
        await self.wait_saved()

    async def wait_saved(self):
        deadline = time.monotonic() + 10

        while True:
            save = (await self.request("GET", "/api/config/save")).get("save")

            if save == "saved":
                return

            if save == "failed":
                raise Failure("the relay could not save /config.json")

            if time.monotonic() > deadline:
                raise Failure(f"/config.json not saved within 10 s, save state \"{save}\"")

            await asyncio.sleep(0.2)

    # A BLE scan on the relay: it sends scan requests to every device in range.
    async def scan(self, seconds):
        await self.request("POST", "/api/ble/scan", {"seconds": seconds}, expect=(202,))
        deadline = time.monotonic() + seconds + 10
        result = {}

        while result.get("state") not in ("done", "failed"):
            if time.monotonic() > deadline:
                raise SetupError("the relay's scan did not end")

            await asyncio.sleep(0.5)
            result = await self.request("GET", "/api/ble/scan/results")

        await self.request("POST", "/api/ble/scan/clear", expect=(200, 409))

        if result["state"] == "failed":
            raise SetupError("the relay's scan failed")

        return result.get("devices", [])

    async def log_end(self):
        _, _, headers = await asyncio.to_thread(self.call, "GET", f"/api/log?since={self.log_next}")
        self.log_next = int(headers.get("X-Log-Next", self.log_next))

        return self.log_next

    async def log_since(self, offset):
        _, raw, _ = await asyncio.to_thread(self.call, "GET", f"/api/log?since={offset}")

        return raw.decode(errors="replace")


# A TCP client of a rule: sends requests, takes the replies apart.
class Client:
    def __init__(self, name, reader, writer):
        self.name = name
        self.reader = reader
        self.writer = writer
        self.buffer = bytearray()

    @staticmethod
    async def connect(name, address, port):
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_connection(address, port), CONNECT_S)
        except (OSError, asyncio.TimeoutError) as error:
            raise Failure(f"{name}: no TCP connection to port {port}: {error or 'timeout'}")

        return Client(name, reader, writer)

    async def send(self, data):
        self.writer.write(data)
        await self.writer.drain()

    # the next whole reply, None when none came within timeout
    async def next_reply(self, timeout):
        deadline = time.monotonic() + timeout

        while True:
            if self.buffer and self.buffer[0] != REPLY:
                raise Failure(f"{self.name}: bytes that start no reply: {bytes(self.buffer[:16]).hex(' ')}")

            if len(self.buffer) >= REPLY_HEADER:
                total = REPLY_HEADER + int.from_bytes(self.buffer[2:4], "little")

                if len(self.buffer) >= total:
                    frame = bytes(self.buffer[:total])
                    del self.buffer[:total]
                    return frame

            left = deadline - time.monotonic()

            if left <= 0:
                return None

            try:
                chunk = await asyncio.wait_for(self.reader.read(4096), left)
            except asyncio.TimeoutError:
                return None
            except ConnectionError as error:
                raise Failure(f"{self.name}: the connection broke: {error}")

            if not chunk:
                raise Failure(f"{self.name}: the relay closed the connection")

            self.buffer += chunk

    async def expect_reply(self, seq, total, timeout):
        frame = await self.next_reply(timeout)

        if frame is None:
            part = f"; {len(self.buffer)} B of it came" if self.buffer else ""
            raise Failure(f"{self.name}: no reply {seq} within {timeout} s{part}")

        expected = make_reply(seq, total)

        if frame[1] != seq or len(frame) != total:
            raise Failure(f"{self.name}: expected reply {seq} of {total} B, got reply {frame[1]} of {len(frame)} B")

        if frame != expected:
            at = next(i for i in range(total) if frame[i] != expected[i])
            raise Failure(f"{self.name}: reply {seq} of {total} B differs from the one sent from byte {at} on")

    async def expect_nothing(self, seconds):
        frame = await self.next_reply(seconds)

        if frame is not None or self.buffer:
            raise Failure(f"{self.name}: expected nothing, got {len(frame or b'') + len(self.buffer)} B")

    async def expect_closed(self, timeout):
        deadline = time.monotonic() + timeout

        while True:
            left = deadline - time.monotonic()

            try:
                chunk = await asyncio.wait_for(self.reader.read(4096), max(left, 0.01))
            except asyncio.TimeoutError:
                raise Failure(f"{self.name}: still connected after {timeout} s")
            except ConnectionError:
                return

            if not chunk:
                return

    async def close(self):
        self.writer.close()

        try:
            await self.writer.wait_closed()
        except OSError:
            pass


class Tests:
    def __init__(self, relay, peripheral, address):
        self.relay = relay
        self.peripheral = peripheral
        self.address = address
        self.target = None
        self.defaults = {}
        self.ports = []
        self.rule_id = None
        self.second_rule_id = None
        self.clients = []

    # Finds this computer in the relay's scan, picks free ports, removes rules a stopped run left.
    async def prepare(self):
        rules = await self.relay.rules()

        for rule in rules:
            if rule["name"].startswith(RULE_NAME):
                await self.relay.remove_rule(rule["id"])
                print(f"removed rule {rule['id']} \"{rule['name']}\", left by an earlier run")

        rules = [rule for rule in rules if not rule["name"].startswith(RULE_NAME)]
        self.defaults = await self.relay.rule_defaults()
        self.defaults.pop("id", None)
        used = {rule["listen_port"] for rule in rules}
        self.ports = [port for port in range(FIRST_PORT, FIRST_PORT + 100) if port not in used][:2]

        print(f"scan: the relay scans {SCAN_S} s for service {SERVICE}")
        devices = await self.relay.scan(SCAN_S)
        device = next((d for d in devices if SERVICE in [s.lower() for s in d.get("services", [])]), None)

        if device is None:
            device = next((d for d in devices if d.get("name", "").lower() == platform.node().lower()), None)

            if device is None:
                raise SetupError(f"none of the {len(devices)} devices the relay's scan saw has the service or the name {platform.node()}; "
                                 "is Bluetooth on here?")

            print(f"scan: the service is not in the advertisement; using the device named like this computer")

        name = device.get("name", "")
        self.target = {"address": "", "name": name} if name else {"address": device["address"], "name": ""}
        print(f"scan: found \"{name}\", {device['address']} ({device.get('address_type')}), {device.get('rssi')} dBm")

        for rule in rules:
            same = rule["target"].get("name") == name if name else rule["target"].get("address") == device["address"]

            if rule["enabled"] and same:
                raise SetupError(f"rule {rule['id']} \"{rule['name']}\" is enabled and targets this computer too: it would share the "
                                 "test's link; disable it for the test")

    # the relay's defaults, and what the test needs
    def body(self, pair, **changes):
        body = dict(self.defaults)
        body.update({
            "name": f"{RULE_NAME} A",
            "enabled": True,
            "listen_port": self.ports[0],
            "max_clients": 1,
            "target": self.target,
            "service_uuid": SERVICE,
            "tx": {"mode": "uuid", "uuid": pair.tx, "write_with_response": pair.with_response},
            "rx": {"mode": "uuid", "uuid": pair.rx, "subscribe": pair.subscribe},
            "coalesce_ms": 20,
            "oversize": "chunk",
            "connect_policy": "on_demand",
            "idle_s": 5,
            "close_tcp_on_ble_loss": False,
            "hexdump": False,
        })
        body.update(changes)

        return body

    # Sets the test's rule to pair and changes (adds it the first time) and waits for its listener.
    async def use_rule(self, pair, **changes):
        body = self.body(pair, **changes)

        if self.rule_id is None:
            self.rule_id = await self.relay.add_rule(body)
        else:
            await self.relay.change_rule(self.rule_id, body)

        if body["enabled"]:
            await self.wait_listening(self.rule_id)

        return self.rule_id

    async def wait_listening(self, rule_id):
        # the relay thread takes the new rules at its next update and closes the old listener first
        await asyncio.sleep(0.5)
        await self.wait_status(rule_id, lambda status: status.get("listening"), "listening", 10)

    async def wait_status(self, rule_id, check, what, timeout):
        deadline = time.monotonic() + timeout
        status = {}

        while time.monotonic() < deadline:
            status = await self.relay.rule_status(rule_id)

            if check(status):
                return status

            await asyncio.sleep(0.25)

        raise Failure(f"rule {rule_id} not {what} within {timeout} s; its status: {json.dumps(status)}")

    async def wait_up(self, rule_id):
        await self.wait_status(rule_id, lambda status: status.get("state") == "up", "up", STATE_S)

    async def connect(self, name, rule_id):
        port = self.ports[1] if rule_id == self.second_rule_id else self.ports[0]
        client = await Client.connect(name, self.address, port)
        self.clients.append(client)

        return client

    async def close_clients(self):
        for client in self.clients:
            await client.close()

        self.clients = []

    async def max_write(self, rule_id):
        mtu = (await self.relay.rule_status(rule_id)).get("mtu", 0)

        if mtu <= 3:
            raise Failure(f"rule {rule_id}: MTU {mtu} while the link is up")

        return mtu - 3

    # Checks what the peripheral got as request seq: on uuid, total bytes, and in how many writes.
    def check_received(self, seq, uuid, total, writes=None, min_writes=None, max_write=None):
        received = self.peripheral.received.get(seq)

        if received is None:
            raise Failure(f"the peripheral got no request {seq}")

        if received.error:
            raise Failure(received.error)

        if received.uuid != uuid or received.total != total:
            raise Failure(f"request {seq} came on {short(received.uuid)} with {received.total} B, expected {short(uuid)} with {total} B")

        sizes = ", ".join(str(size) for size in received.writes)

        if writes is not None and len(received.writes) != writes:
            raise Failure(f"request {seq} of {total} B came in {len(received.writes)} writes ({sizes} B), expected {writes}")

        if min_writes is not None and len(received.writes) < min_writes:
            raise Failure(f"request {seq} of {total} B came in {len(received.writes)} writes ({sizes} B), expected {min_writes} or more")

        if max_write is not None and max(received.writes) > max_write:
            raise Failure(f"request {seq} came in writes of {sizes} B; one write takes at most {max_write} B")

        return received

    # Connects, waits for the link and has request 1 answered; returns the client and MTU - 3.
    async def open_link(self, rule_id, pair):
        client = await self.connect("client", rule_id)
        await self.wait_up(rule_id)
        await client.send(make_request(1, 16, 20))
        await client.expect_reply(1, 20, REPLY_S)
        self.check_received(1, pair.tx, 16, writes=1)

        return client, await self.max_write(rule_id)

    async def queued_first_request(self):
        rule_id = await self.use_rule(CONFIRMED)
        client = await self.connect("client", rule_id)
        started = time.monotonic()
        await client.send(make_request(1, 16, 20))
        await client.expect_reply(1, 20, FIRST_REPLY_S)
        waited = time.monotonic() - started
        self.check_received(1, TX1, 16, writes=1)
        max_write = await self.max_write(rule_id)
        quarter = max(REQUEST_HEADER, max_write // 4)
        half = max(REQUEST_HEADER, max_write // 2)
        sizes = [(6, 4), (quarter, 20), (half, 100), (max_write, max_write), (max_write, 2 * max_write + 10)]

        for seq, (request, reply) in enumerate(sizes, start=2):
            await client.send(make_request(seq, request, reply))
            await client.expect_reply(seq, reply, REPLY_S)
            self.check_received(seq, TX1, request, writes=1)

        return (f"first reply {waited:.1f} s after the connect, the request waited in the relay for the link; MTU {max_write + 3}; "
                f"requests of 6-{max_write} B one write each, replies of 4-{2 * max_write + 10} B")

    async def burst_without_response(self):
        rule_id = await self.use_rule(UNCONFIRMED)
        client, max_write = await self.open_link(rule_id, UNCONFIRMED)
        burst = [(seq, REQUEST_HEADER + seq * 5 % 40, REPLY_HEADER + seq * 7 % 60) for seq in range(2, 32)]
        stream = b"".join(make_request(seq, request, reply) for seq, request, reply in burst)
        inbox = self.peripheral.inboxes[TX2]
        writes_before = len(inbox.write_sizes)
        inbox.expect(stream)
        await client.send(stream)

        for seq, request, reply in burst:
            await client.expect_reply(seq, reply, REPLY_S)
            self.check_received(seq, TX2, request, max_write=max_write)

        writes = len(inbox.write_sizes) - writes_before
        early = f", {inbox.early} came here before an earlier one and waited for it" if inbox.early else ""

        return f"{len(burst)} requests, {len(stream)} B in one TCP send, came in {writes} writes without response{early}; the replies came in order"

    async def chunked(self):
        rule_id = await self.use_rule(CONFIRMED, oversize="chunk")
        client, max_write = await self.open_link(rule_id, CONFIRMED)
        total = 3 * max_write + 17
        await client.send(make_request(2, total, total))
        await client.expect_reply(2, total, REPLY_S)
        received = self.check_received(2, TX1, total, min_writes=math.ceil(total / max_write), max_write=max_write)
        sizes = ", ".join(str(size) for size in received.writes)

        return f"a {total} B request came in {len(received.writes)} writes ({sizes} B); its {total} B reply came whole"

    async def long_write(self):
        rule_id = await self.use_rule(CONFIRMED, oversize="long_write")
        client, max_write = await self.open_link(rule_id, CONFIRMED)
        total = min(MAX_ATTRIBUTE_VALUE, max_write + 100)

        if total <= max_write:
            raise Failure(f"MTU {max_write + 3}: a packet up to {MAX_ATTRIBUTE_VALUE} B fits one write, no long write to test")

        await client.send(make_request(2, total, 30))
        await client.expect_reply(2, 30, REPLY_S)
        received = self.check_received(2, TX1, total)
        before = (await self.relay.rule_status(rule_id)).get("oversize_events", 0)
        too_long = MAX_ATTRIBUTE_VALUE + 88
        await client.send(make_request(3, too_long, 30))
        await client.expect_nothing(QUIET_S)

        if 3 in self.peripheral.received:
            raise Failure(f"the peripheral got request 3 of {too_long} B, longer than a long write takes")

        await self.wait_status(rule_id, lambda status: status.get("oversize_events", 0) == before + 1, "counting the dropped packet", 2)
        await client.send(make_request(4, 20, 20))
        await client.expect_reply(4, 20, REPLY_S)
        count = len(received.writes)

        return f"{total} B went as one long write ({count} write{'s' if count > 1 else ''} at the peripheral); {too_long} B dropped and counted"

    async def two_clients(self):
        rule_id = await self.use_rule(CONFIRMED, max_clients=2)
        first = await self.connect("client 1", rule_id)
        second = await self.connect("client 2", rule_id)
        await self.wait_status(rule_id, lambda status: len(status.get("clients", [])) == 2, "with 2 clients", 5)
        await self.wait_up(rule_id)
        await first.send(make_request(1, 30, 40))
        await first.expect_reply(1, 40, REPLY_S)
        await second.expect_reply(1, 40, REPLY_S)
        await second.send(make_request(2, 50, 60))
        await second.expect_reply(2, 60, REPLY_S)
        await first.expect_reply(2, 60, REPLY_S)
        await self.peripheral.send(RX1, make_reply(0xF0, 70))
        await first.expect_reply(0xF0, 70, REPLY_S)
        await second.expect_reply(0xF0, 70, REPLY_S)
        third = await self.connect("client 3", rule_id)
        await third.expect_closed(3)
        status = await self.wait_status(rule_id, lambda status: "max_clients" in status.get("tcp_error", ""), "telling the refusal", 3)

        return f"both clients got both replies and one unasked frame; a third was closed at once: \"{status['tcp_error']}\""

    async def two_rules(self):
        rule_id = await self.use_rule(CONFIRMED)
        body = self.body(UNCONFIRMED, name=f"{RULE_NAME} B", listen_port=self.ports[1])
        self.second_rule_id = await self.relay.add_rule(body)
        await self.wait_listening(rule_id)
        await self.wait_listening(self.second_rule_id)
        first = await self.connect(f"client of {rule_id}", rule_id)
        second = await self.connect(f"client of {self.second_rule_id}", self.second_rule_id)
        await self.wait_up(rule_id)
        await first.send(make_request(1, 24, 24))
        await first.expect_reply(1, 24, REPLY_S)
        await second.send(make_request(2, 30, 30))
        await second.expect_reply(2, 30, REPLY_S)
        await first.expect_nothing(1)
        await second.expect_nothing(1)
        self.check_received(1, TX1, 24)
        self.check_received(2, TX2, 30)
        statuses = [await self.relay.rule_status(rule_id), await self.relay.rule_status(self.second_rule_id)]

        if len({status.get("address") for status in statuses}) != 1:
            raise Failure(f"the rules are on different links: {[status.get('address') for status in statuses]}")

        return f"rules {rule_id} and {self.second_rule_id} on one link to {statuses[0].get('address')}, each got only its own reply"

    async def idle_disconnect(self):
        idle_s = 2
        rule_id = await self.use_rule(CONFIRMED, idle_s=idle_s)
        client, _ = await self.open_link(rule_id, CONFIRMED)
        await client.close()
        closed = time.monotonic()
        await self.wait_status(rule_id, lambda status: status.get("state") == "idle", "idle after its client left", idle_s + 10)
        after = time.monotonic() - closed

        if after < idle_s - 0.5:
            raise Failure(f"the link closed {after:.1f} s after the client left, before idle_s {idle_s}")

        client = await self.connect("new client", rule_id)
        await self.wait_up(rule_id)
        await client.send(make_request(2, 16, 20))
        await client.expect_reply(2, 20, REPLY_S)

        return f"the link closed {after:.1f} s after the last client left (idle_s {idle_s}); a new client brought it back"

    async def always_then_disable(self):
        rule_id = await self.use_rule(CONFIRMED, connect_policy="always")
        await self.wait_up(rule_id)
        client = await self.connect("client", rule_id)
        await self.wait_status(rule_id, lambda status: len(status.get("clients", [])) == 1, "with the client", 5)
        await self.peripheral.send(RX1, make_reply(0xF1, 40))
        await client.expect_reply(0xF1, 40, REPLY_S)
        await self.relay.change_rule(rule_id, self.body(CONFIRMED, connect_policy="always", enabled=False))
        await client.expect_closed(5)
        await self.wait_status(rule_id, lambda status: status.get("state") == "disabled", "disabled", 5)

        try:
            late = await Client.connect("late client", self.address, self.ports[0])
        except Failure:
            late = None

        if late is not None:
            await late.close()
            raise Failure(f"port {self.ports[0]} still takes connections with the rule disabled")

        return "up with no client; an unasked frame reached the client; disabling closed it and the port"

    SCENARIOS = [
        ("a request sent before the link is up; write with response, indicate; sizes up to MTU - 3", queued_first_request),
        ("a burst of requests; write without response, notify", burst_without_response),
        ("oversize chunk: a request of 3 writes and more, a reply of several indications", chunked),
        ("oversize long_write: one long write; beyond 512 B dropped", long_write),
        ("two clients get every reply; a third is refused", two_clients),
        ("two rules on one device, each with its own characteristics", two_rules),
        ("on demand: the link closes idle_s after the last client, and comes back", idle_disconnect),
        ("policy always: up without a client; disabling closes the client", always_then_disable),
    ]

    async def run(self, index, title, scenario):
        print(f"\n[{index}/{len(self.SCENARIOS)}] {title}")
        self.peripheral.clear()
        offset = await self.relay.log_end()

        try:
            detail = await scenario(self)

            if self.peripheral.errors:
                raise Failure("the peripheral: " + "; ".join(self.peripheral.errors))

            print(f"  PASS  {detail}")
            return True
        except Failure as problem:
            print(f"  FAIL  {problem}")
            await self.report(offset)
            return False
        finally:
            await self.close_clients()

            if self.second_rule_id is not None:
                try:
                    await self.relay.remove_rule(self.second_rule_id)
                    self.second_rule_id = None
                except Failure as problem:
                    print(f"        removing rule {self.second_rule_id} failed: {problem}")

    async def report(self, offset):
        try:
            await self.print_report(offset)
        except Failure as problem:
            print(f"        no more details: {problem}")

    async def print_report(self, offset):
        for error in self.peripheral.errors:
            print(f"        the peripheral: {error}")

        for seq, received in sorted(self.peripheral.received.items()):
            error = f"; {received.error}" if received.error else ""
            print(f"        the peripheral got request {seq}: {received.total} B on {short(received.uuid)} in writes of {received.writes} B{error}")

        for uuid, inbox in self.peripheral.inboxes.items():
            if inbox.held:
                print(f"        the peripheral: {short(uuid)} waits for byte {inbox.position} of the burst; it holds the writes at bytes "
                      f"{sorted(inbox.held)}, which came before it")

        for rule_id in (self.rule_id, self.second_rule_id):
            if rule_id is not None:
                print(f"        rule {rule_id}: {json.dumps(await self.relay.rule_status(rule_id))}")

        lines = (await self.relay.log_since(offset)).splitlines()[-40:]
        print("        the relay's log since this test started:")

        for line in lines:
            print(f"          {line}")

    async def remove_rules(self):
        for rule_id in (self.second_rule_id, self.rule_id):
            if rule_id is not None:
                await self.relay.remove_rule(rule_id)
                print(f"removed rule {rule_id}")


def resolve():
    try:
        return socket.getaddrinfo(HOST, HTTP_PORT, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
    except socket.gaierror as error:
        print(f"{HOST} does not resolve ({error}); no test ran")
        sys.exit(2)


async def run(address):
    relay = Relay(address)
    device = (await relay.status()).get("device", {})
    print(f"{HOST} is {address}: firmware {device.get('firmware')}, BLE address {device.get('ble_address')}, "
          f"heap free {device.get('heap_free', 0) // 1024} KB")
    peripheral = Peripheral()
    await peripheral.start()
    print(f"peripheral: service {SERVICE} advertised from this computer")
    tests = Tests(relay, peripheral, address)
    results = []

    try:
        try:
            await tests.prepare()
        except Failure as problem:
            raise SetupError(str(problem))

        for index, (title, scenario) in enumerate(Tests.SCENARIOS, start=1):
            results.append(await tests.run(index, title, scenario))
    finally:
        try:
            await tests.close_clients()
            await tests.remove_rules()
        except (Failure, RelayGone) as problem:
            print(f"cleaning up failed: {problem}")

        await peripheral.stop()

    passed = results.count(True)
    print(f"\n{len(results)} tests: {passed} passed, {len(results) - passed} failed")

    return 0 if passed == len(results) else 1


def main():
    address = resolve()

    try:
        return asyncio.run(run(address))
    except (SetupError, Failure) as problem:
        print(f"stopped: {problem}")
    except RelayGone as problem:
        print(f"stopped, the relay does not answer: {problem}")
    except KeyboardInterrupt:
        print("stopped")

    return 2


if __name__ == "__main__":
    sys.exit(main())
