# The relay's stress test: how many TCP clients through how many BLE devices it takes before it drops frames, runs out of memory
# for an answer or restarts. A GATT server on this computer is one device; every other device in range advertising the test
# service joins the ladder. --heavy: bigger frames, more often.
import asyncio
import dataclasses
import http.client
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

# the test's own GATT service and characteristics, none of the HF-LPT270's; the same as the integration test's
SERVICE = "7d2a1000-4c1e-4b8a-9a55-2f1e6b7c0d00"
TX1 = "7d2a1001-4c1e-4b8a-9a55-2f1e6b7c0d00"
RX1 = "7d2a1002-4c1e-4b8a-9a55-2f1e6b7c0d00"
TX2 = "7d2a1003-4c1e-4b8a-9a55-2f1e6b7c0d00"
RX2 = "7d2a1004-4c1e-4b8a-9a55-2f1e6b7c0d00"

# the test's rules are named so; one left by a stopped run is removed at the start
RULE_NAME = "w2b-stress"

# the rules' ports, one block per device, from here on
FIRST_PORT = 18951

PORTS_PER_DEVICE = 8

SCAN_S = 8

HTTP_TIMEOUT_S = 5

CONNECT_S = 5

# every link up before a step's traffic starts
LINK_S = 40

# the traffic of one step
STEP_S = 15

# --heavy: bigger frames, more often; the default is the traffic of a polling client
HEAVY = "--heavy" in sys.argv[1:]

# --rules: one client per rule and the ladder climbs rules, the memory shape of that many devices; else clients per device
RULES = "--rules" in sys.argv[1:]

# every client sends a request this often: regular traffic, no flood
REQUEST_PERIOD_S = 1.0 if HEAVY else 2.0

REQUEST_BYTES = 200 if HEAVY else 24

REPLY_BYTES = 480 if HEAVY else 48

# a reply later than this counts as lost
REPLY_S = 5

# the page's polling, imitated during a step
STATUS_PERIOD_S = 5

LOG_PERIOD_S = 2

# how long a restarted relay gets to come back
BACK_S = 60

# how many of the relay's last log lines a failed step shows
LOG_TAIL_LINES = 20

# a request: A5, sequence number, payload length (2 bytes LE), reply length (2 bytes LE), payload
REQUEST = 0xA5

REQUEST_HEADER = 6

# a reply: 5A, sequence number, payload length (2 bytes LE), payload
REPLY = 0x5A

REPLY_HEADER = 4

# the payload starts with the client's number and its request counter, 2 bytes LE each; the reply carries them back
TAG_BYTES = 4


@dataclasses.dataclass(frozen=True)
class Pair:
    tx: str
    rx: str
    with_response: bool
    subscribe: str


CONFIRMED = Pair(TX1, RX1, True, "indicate")

UNCONFIRMED = Pair(TX2, RX2, False, "notify")

ANSWER_ON = {TX1: RX1, TX2: RX2}


class SetupError(Exception):
    pass


# the relay did not answer HTTP
class RelayGone(Exception):
    pass


def short(uuid):
    return uuid[4:8]


def filler(seq, size):
    return bytes((seq * 7 + i * 13 + 1) & 0xFF for i in range(size))


def make_request(seq, client, counter, total=REQUEST_BYTES, reply_total=REPLY_BYTES):
    size = total - REQUEST_HEADER
    tag = client.to_bytes(2, "little") + counter.to_bytes(2, "little")

    return (bytes([REQUEST, seq]) + size.to_bytes(2, "little") + (reply_total - REPLY_HEADER).to_bytes(2, "little")
            + tag + filler(seq, size - TAG_BYTES))


def make_reply(seq, tag, total):
    size = total - REPLY_HEADER

    return bytes([REPLY, seq]) + size.to_bytes(2, "little") + tag + filler(seq, size - TAG_BYTES)


# Whole frames out of a stream: (seq, tag, reply_total) per request, or (seq, tag) per reply.
def take_frames(buffer, start, header):
    frames = []

    while buffer:
        if buffer[0] != start:
            cut = buffer.find(bytes([start]))
            del buffer[:len(buffer) if cut < 0 else cut]
            continue

        if len(buffer) < header:
            break

        size = int.from_bytes(buffer[2:4], "little")
        total = header + size

        if len(buffer) < total:
            break

        seq = buffer[1]
        payload = bytes(buffer[header:total])
        tag = payload[:TAG_BYTES]

        if start == REQUEST:
            frames.append((seq, tag, REPLY_HEADER + int.from_bytes(buffer[4:6], "little")))
        else:
            frames.append((seq, tag))

        del buffer[:total]

    return frames


# The test's GATT server on this computer (bless): answers every request that arrives on a TX on the paired RX.
class Peripheral:
    def __init__(self):
        self.loop = None
        self.server = None
        self.data_writer = None
        self.sending = None
        self.buffers = {TX1: bytearray(), TX2: bytearray()}
        self.requests = 0
        self.replies = 0
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
        self.requests = 0
        self.replies = 0
        self.errors = []

    def on_read(self, characteristic, **kwargs):
        return characteristic.value or bytearray()

    # bless's thread: the bytes go to the event loop
    def on_write(self, characteristic, value, **kwargs):
        self.loop.call_soon_threadsafe(self.take, str(characteristic.uuid).lower(), bytes(value))

    def take(self, uuid, chunk):
        buffer = self.buffers.get(uuid)

        if buffer is None:
            self.errors.append(f"a write to {short(uuid)}, not a TX of the test")
            return

        buffer += chunk

        for seq, tag, reply_total in take_frames(buffer, REQUEST, REQUEST_HEADER):
            self.requests += 1
            asyncio.ensure_future(self.send(ANSWER_ON[uuid], make_reply(seq, tag, reply_total)))

    # Sends frame on rx in pieces the subscribers take: an indication or notification each, one frame after the other.
    async def send(self, rx, frame):
        async with self.sending:
            characteristic = self.server.get_characteristic(rx)
            piece = await self.piece_size(characteristic)

            if piece == 0:
                self.errors.append(f"nobody subscribed to {short(rx)}: a reply not sent")
                return

            for at in range(0, len(frame), piece):
                await self.notify(rx, characteristic, frame[at:at + piece])

            self.replies += 1

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
                self.errors.append(f"{short(rx)}: sending {len(piece)} B failed, status {int(result.status)}")


# The relay's web API.
class Relay:
    def __init__(self, address):
        self.base = f"http://{address}:{HTTP_PORT}"
        self.log_next = 0

    def call(self, method, path, body=None, timeout=HTTP_TIMEOUT_S):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"} if data is not None else {}
        request = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)

        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.status, response.read(), response.headers
        except urllib.error.HTTPError as error:
            return error.code, error.read(), error.headers
        except (OSError, http.client.HTTPException) as error:
            raise RelayGone(f"{method} {path}: {error}")

    async def request(self, method, path, body=None, expect=(200,)):
        code, raw, _ = await asyncio.to_thread(self.call, method, path, body)

        try:
            data = json.loads(raw) if raw else {}
        except ValueError:
            data = {"error": raw[:200].decode(errors="replace")}

        if code not in expect:
            raise SetupError(f"{method} {path}: HTTP {code}, {data.get('error', data)}")

        return data

    async def status(self):
        return await self.request("GET", "/api/status")

    # the status, or None when the relay does not answer
    async def try_status(self):
        try:
            return await self.status()
        except (RelayGone, SetupError):
            return None

    async def rules(self):
        return await self.request("GET", "/api/rules")

    # Changes the relay's state: adds a rule, which writes its /config.json; returns the id the relay gave it.
    async def add_rule(self, body):
        await self.request("POST", "/api/rules", body, expect=(202,))
        await self.wait_saved()
        ids = [rule["id"] for rule in (await self.rules())["rules"] if rule["name"] == body["name"]]

        if not ids:
            raise SetupError(f"the rule \"{body['name']}\" is not in /api/rules after its POST")

        return ids[-1]

    # Changes the relay's state: replaces a rule, which writes its /config.json.
    async def change_rule(self, rule_id, body):
        await self.request("PUT", f"/api/rules/{rule_id}", body, expect=(202,))
        await self.wait_saved()

    # Changes the relay's state: removes a rule, which writes its /config.json.
    async def remove_rule(self, rule_id):
        await self.request("DELETE", f"/api/rules/{rule_id}", expect=(202, 404))
        await self.wait_saved()

    async def wait_saved(self):
        deadline = time.monotonic() + 10

        while True:
            save = (await self.request("GET", "/api/config/save")).get("save")

            if save == "saved":
                return

            if save == "failed":
                raise SetupError("the relay could not save /config.json")

            if time.monotonic() > deadline:
                raise SetupError(f"/config.json not saved within 10 s, save state \"{save}\"")

            await asyncio.sleep(0.2)

    # A BLE scan on the relay: it sends scan requests to every device in range, the inverter's module too.
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

    # the page's log poll: what came since the last one
    async def poll_log(self):
        _, raw, headers = await asyncio.to_thread(self.call, "GET", f"/api/log?since={self.log_next}")
        self.log_next = int(headers.get("X-Log-Next", self.log_next))

        return raw.decode(errors="replace")

    async def page(self):
        await asyncio.to_thread(self.call, "GET", "/")


# A TCP client of a rule: a request every REQUEST_PERIOD_S, its own replies matched by the tag, the others' ignored.
class Client:
    def __init__(self, number, rule, address, port):
        self.number = number
        self.rule = rule
        self.address = address
        self.port = port
        self.reader = None
        self.writer = None
        self.pending = {}
        self.sent = 0
        self.got = 0
        self.lost = 0
        self.max_latency = 0.0
        self.error = ""

    async def connect(self):
        try:
            self.reader, self.writer = await asyncio.wait_for(asyncio.open_connection(self.address, self.port), CONNECT_S)
        except (OSError, asyncio.TimeoutError) as error:
            self.error = f"no TCP connection to port {self.port}: {error or 'timeout'}"

        return not self.error

    # Sends until the deadline, reads meanwhile; then counts the replies still missing as lost.
    async def run(self, until):
        reading = asyncio.ensure_future(self.read())
        counter = 0

        try:
            while time.monotonic() < until and not self.error:
                counter = (counter + 1) & 0xFFFF
                self.pending[counter] = time.monotonic()
                self.sent += 1

                try:
                    self.writer.write(make_request(counter & 0xFF, self.number, counter))
                    await self.writer.drain()
                except (ConnectionError, OSError) as error:
                    self.error = f"the connection broke: {error}"
                    break

                self.expire()
                await asyncio.sleep(REQUEST_PERIOD_S)

            await asyncio.sleep(min(REPLY_S, max(0.0, until + REPLY_S - time.monotonic())))
        finally:
            reading.cancel()
            self.expire(all_of_them=True)

    async def read(self):
        buffer = bytearray()

        while True:
            try:
                chunk = await self.reader.read(4096)
            except (ConnectionError, OSError, asyncio.CancelledError):
                return

            if not chunk:
                self.error = self.error or "the relay closed the connection"
                return

            buffer += chunk

            for _, tag in take_frames(buffer, REPLY, REPLY_HEADER):
                if int.from_bytes(tag[:2], "little") != self.number:
                    continue

                sent = self.pending.pop(int.from_bytes(tag[2:4], "little"), None)

                if sent is not None:
                    self.got += 1
                    self.max_latency = max(self.max_latency, time.monotonic() - sent)

    def expire(self, all_of_them=False):
        now = time.monotonic()

        for counter, sent in list(self.pending.items()):
            if all_of_them or now - sent > REPLY_S:
                del self.pending[counter]
                self.lost += 1

    async def close(self):
        if self.writer is None:
            return

        self.writer.close()

        try:
            await self.writer.wait_closed()
        except OSError:
            pass


# What one step of the ladder showed.
@dataclasses.dataclass
class StepResult:
    clients_per_device: int
    rules_per_device: int
    outcome: str
    heap_free: int = 0
    heap_min: int = 0
    heap_largest: int = 0
    lost: int = 0
    relay_dropped: int = 0
    max_latency: float = 0.0
    problems: list = dataclasses.field(default_factory=list)
    log: list = dataclasses.field(default_factory=list)

    @property
    def clean(self):
        return self.outcome == "ok" and self.lost == 0 and self.relay_dropped == 0 and not self.problems


# The page's polling, imitated during a step: the status every STATUS_PERIOD_S, the log every LOG_PERIOD_S, one at a time.
class Page:
    def __init__(self, relay):
        self.relay = relay
        self.heap_free = None
        self.heap_min = None
        self.heap_largest = None
        self.uptime = None
        self.restarted = False
        self.gone = 0
        self.failures = []
        self.log_lines = []

    async def run(self, until):
        try:
            await self.relay.page()
        except RelayGone as problem:
            self.fail(problem)

        next_status = 0.0

        while time.monotonic() < until:
            try:
                if time.monotonic() >= next_status:
                    self.note(await self.relay.status())
                    next_status = time.monotonic() + STATUS_PERIOD_S

                self.log_lines += (await self.relay.poll_log()).splitlines()
                self.gone = 0
            except (RelayGone, SetupError) as problem:
                self.fail(problem)

                if self.gone >= 3:
                    return

            await asyncio.sleep(LOG_PERIOD_S)

    def fail(self, problem):
        self.gone += 1
        self.failures.append(f"{time.strftime('%H:%M:%S')} {problem}")

    def note(self, status):
        device = status.get("device", {})
        uptime = device.get("uptime_s", 0)

        if self.uptime is not None and uptime < self.uptime:
            self.restarted = True

        self.uptime = uptime
        free = device.get("heap_free", 0)
        self.heap_free = free if self.heap_free is None else min(self.heap_free, free)
        self.heap_min = device.get("heap_min_free", 0)
        self.heap_largest = device.get("heap_largest", 0)


# The ladder: k TCP clients on every device, k from 1 up, over as many rules per device as the clients need.
class Stress:
    def __init__(self, relay, peripheral, address):
        self.relay = relay
        self.peripheral = peripheral
        self.address = address
        self.devices = []
        self.defaults = {}
        self.max_clients = 1
        self.max_rules = 1
        # (device index, rule index) -> rule id
        self.rule_ids = {}
        self.results = []

    # Removes rules a stopped run left, reads the relay's limits, finds every peripheral in the relay's scan.
    async def prepare(self):
        listing = await self.relay.rules()

        for rule in listing["rules"]:
            if rule["name"].startswith(RULE_NAME):
                await self.relay.remove_rule(rule["id"])
                print(f"removed rule {rule['id']} \"{rule['name']}\", left by an earlier run")

        others = [rule for rule in listing["rules"] if not rule["name"].startswith(RULE_NAME)]
        self.defaults = dict(listing["defaults"])
        self.defaults.pop("id", None)
        self.max_clients = listing["limits"]["clients"]
        self.max_rules = listing["limits"]["enabled"] - sum(1 for rule in others if rule["enabled"])
        used = {rule["listen_port"] for rule in others}

        if any(FIRST_PORT <= port < FIRST_PORT + PORTS_PER_DEVICE * 16 for port in used):
            raise SetupError(f"a rule of yours listens in the test's port range from {FIRST_PORT}")

        print(f"scan: the relay scans {SCAN_S} s for service {SERVICE}")
        found = await self.relay.scan(SCAN_S)
        mine = platform.node().lower()

        for device in found:
            services = [service.lower() for service in device.get("services", [])]
            name = device.get("name", "")

            if SERVICE in services or name.lower() == mine:
                target = {"address": "", "name": name} if name.lower() == mine and name else {"address": device["address"], "name": ""}
                self.devices.append(target)
                print(f"scan: found \"{name}\", {device['address']} ({device.get('address_type')}), {device.get('rssi')} dBm")

        if not self.devices:
            raise SetupError(f"none of the {len(found)} devices the relay's scan saw has the service or the name {mine}; is Bluetooth on here?")

        for rule in others:
            if rule["enabled"] and rule["target"] in self.devices:
                raise SetupError(f"rule {rule['id']} \"{rule['name']}\" is enabled and targets a test device too; disable it for the test")

    def body(self, device, index, clients):
        pair = CONFIRMED if index % 2 == 0 else UNCONFIRMED
        body = dict(self.defaults)
        body.update({
            "name": f"{RULE_NAME} {device + 1}.{index + 1}",
            "enabled": True,
            "listen_port": FIRST_PORT + device * PORTS_PER_DEVICE + index,
            "max_clients": clients,
            "target": self.devices[device],
            "service_uuid": SERVICE,
            "tx": {"mode": "uuid", "uuid": pair.tx, "write_with_response": pair.with_response},
            "rx": {"mode": "uuid", "uuid": pair.rx, "subscribe": pair.subscribe},
            "coalesce_ms": 20,
            "oversize": "chunk",
            "connect_policy": "on_demand",
            "idle_s": 30,
            "close_tcp_on_ble_loss": False,
            "hexdump": False,
        })

        return body

    # the rules for step k: with --rules k of them with one client each; else as many as k clients need, the last with the rest
    def layout(self, k):
        if RULES:
            return [1] * k

        rules = math.ceil(k / self.max_clients)

        return [self.max_clients if index < rules - 1 else k - self.max_clients * (rules - 1) for index in range(rules)]

    async def run(self):
        k = 1

        while True:
            counts = self.layout(k)

            if len(counts) * len(self.devices) > self.max_rules:
                print(f"\nthe relay's limit of {self.max_rules} enabled rules is reached: {k} clients per device would need {len(counts)} rules each")
                break

            result = await self.step(k, counts)
            self.results.append(result)

            if result.outcome != "ok":
                break

            k += 1

    async def step(self, k, counts):
        result = StepResult(sum(counts), len(counts), "ok")
        print(f"\n[{sum(counts)} client(s) per device, {len(counts)} rule(s) each, {len(self.devices)} device(s)]")
        self.peripheral.clear()
        clients = []
        start_uptime = 0

        try:
            start_uptime = (await self.relay.status()).get("device", {}).get("uptime_s", 0)
            await self.apply_rules(counts)
            clients = await self.connect_clients(counts)
            await self.wait_links()
            until = time.monotonic() + STEP_S
            page = Page(self.relay)
            await asyncio.gather(page.run(until), *(client.run(until) for client in clients))
            await self.collect(result, clients, page)
        except (RelayGone, SetupError) as problem:
            result.outcome = "gone"
            result.problems.append(str(problem))
        finally:
            for client in clients:
                await client.close()

        if result.outcome in ("gone", "restarted"):
            await self.after_restart(result, start_uptime)

        self.print_step(result)

        return result

    async def apply_rules(self, counts):
        for device in range(len(self.devices)):
            for index in range(len(counts)):
                body = self.body(device, index, counts[index])
                key = (device, index)

                if key in self.rule_ids:
                    await self.relay.change_rule(self.rule_ids[key], body)
                else:
                    self.rule_ids[key] = await self.relay.add_rule(body)

        # the relay thread takes the new rules at its next update and opens the listeners
        await asyncio.sleep(1)

    async def connect_clients(self, counts):
        clients = []
        number = 1

        for device in range(len(self.devices)):
            for index, count in enumerate(counts):
                for _ in range(count):
                    client = Client(number, (device, index), self.address, FIRST_PORT + device * PORTS_PER_DEVICE + index)
                    number += 1

                    if not await client.connect():
                        raise SetupError(f"client {client.number}: {client.error}")

                    clients.append(client)

        return clients

    async def wait_links(self):
        deadline = time.monotonic() + LINK_S
        waiting = set(self.rule_ids.values())

        while waiting and time.monotonic() < deadline:
            relays = (await self.relay.status()).get("relays", [])
            waiting = {rule_id for rule_id in waiting if next((r for r in relays if r.get("id") == rule_id), {}).get("state") != "up"}
            await asyncio.sleep(0.5)

        if waiting:
            raise SetupError(f"{len(waiting)} link(s) not up within {LINK_S} s")

    async def collect(self, result, clients, page):
        result.lost = sum(client.lost for client in clients)
        result.max_latency = max((client.max_latency for client in clients), default=0.0)
        result.problems += [f"client {client.number}: {client.error}" for client in clients if client.error]
        result.problems += self.peripheral.errors[:5]
        result.problems += page.failures
        result.log = page.log_lines

        if page.gone >= 3:
            result.outcome = "gone"
            return

        try:
            status = await self.relay.status()
        except (RelayGone, SetupError) as problem:
            result.outcome = "gone"
            result.problems.append(f"the status after the step: {problem}")
            return

        page.note(status)
        result.heap_free = page.heap_free or 0
        result.heap_min = page.heap_min or 0
        result.heap_largest = page.heap_largest or 0
        result.relay_dropped = sum(r.get("rx_dropped", 0) + r.get("tx_dropped", 0) for r in status.get("relays", [])
                                   if r.get("id") in self.rule_ids.values())

        if page.restarted:
            result.outcome = "restarted"

    # Waits for the relay to come back; restarted when its uptime went back, else it was only unreachable. After a restart reads
    # why and removes the rules, so it does not restart again with them.
    async def after_restart(self, result, start_uptime):
        deadline = time.monotonic() + BACK_S
        status = None

        while status is None and time.monotonic() < deadline:
            await asyncio.sleep(2)
            status = await self.relay.try_status()

        if status is None:
            result.outcome = "gone for good"
            return

        if status.get("device", {}).get("uptime_s", 0) >= start_uptime:
            result.outcome = "no answer"
            result.problems.insert(0, "the relay stopped answering the page's requests and did not restart:")
            await self.read_log(result)
            return

        result.outcome = "restarted"
        self.relay.log_next = 0

        try:
            reason = next((line.split("reset reason", 1)[1].strip() for line in (await self.relay.poll_log()).splitlines()
                           if "reset reason" in line), "")
            result.problems.insert(0, f"reset reason {reason}" if reason else "restarted")
            await self.remove_rules()
        except (RelayGone, SetupError) as problem:
            result.problems.append(f"after the restart: {problem}")

    # the relay's log lines since the page last read them, for a failed step's report
    async def read_log(self, result):
        try:
            result.log += (await self.relay.poll_log()).splitlines()
        except (RelayGone, SetupError) as problem:
            result.problems.append(f"reading the log: {problem}")

    async def remove_rules(self):
        for key, rule_id in list(self.rule_ids.items()):
            await self.relay.remove_rule(rule_id)
            del self.rule_ids[key]

    @staticmethod
    def print_step(result):
        heap = f"heap free {result.heap_free // 1024} KB, min {result.heap_min // 1024} KB, largest {result.heap_largest // 1024} KB"
        traffic = f"lost replies {result.lost}, relay drops {result.relay_dropped}, slowest reply {result.max_latency:.2f} s"
        print(f"  {result.outcome.upper():10} {heap}; {traffic}")

        for problem in result.problems:
            print(f"             {problem}")

        if result.outcome != "ok" and result.log:
            print(f"             the relay's log, its last {LOG_TAIL_LINES} lines:")

            for line in result.log[-LOG_TAIL_LINES:]:
                print(f"               {line}")

    def report(self):
        devices = len(self.devices)
        print("\n=== Report ===")
        print(f"peripherals: {devices}" + (" (this computer)" if devices == 1 else ""))
        print(f"{'clients/device':>14} {'rules/device':>12} {'outcome':10} {'free':>6} {'min':>6} {'largest':>8} {'lost':>5} {'drops':>6} {'slowest':>8}")

        for r in self.results:
            print(f"{r.clients_per_device:>14} {r.rules_per_device:>12} {r.outcome:10} {r.heap_free // 1024:>4} KB {r.heap_min // 1024:>3} KB "
                  f"{r.heap_largest // 1024:>5} KB {r.lost:>5} {r.relay_dropped:>6} {r.max_latency:>7.2f}s")

        ok = [r for r in self.results if r.outcome == "ok"]
        clean = [r for r in self.results if r.clean]
        each = "each " if devices > 1 else ""

        if not ok:
            print(f"\n{devices} BLE device(s): not even 1 TCP client {each}held; see the first step")
            return

        best = ok[-1]

        if RULES:
            print(f"\n{devices} BLE device(s) with up to {best.rules_per_device} rules {each}of 1 TCP client, the memory shape of "
                  f"{best.rules_per_device * devices} devices, without a restart"
                  + (f"; {clean[-1].rules_per_device} without a lost reply or a dropped frame" if clean else
                     "; every step lost a reply or dropped a frame"))
        else:
            print(f"\n{devices} BLE device(s) with up to {best.clients_per_device} TCP clients {each}({best.rules_per_device} rule(s) per device) "
                  f"without a restart" + (f"; {clean[-1].clients_per_device} without a lost reply or a dropped frame" if clean else
                                          "; every step lost a reply or dropped a frame"))

        if len(ok) == len(self.results):
            print("the ladder ended at the relay's rule limit, not at a restart")


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
    stress = Stress(relay, peripheral, address)
    print(f"traffic: a request of {REQUEST_BYTES} B every {REQUEST_PERIOD_S:g} s from every client, a reply of {REPLY_BYTES} B"
          + (" (--heavy)" if HEAVY else "") + ("; the ladder climbs rules of one client (--rules)" if RULES else ""))

    try:
        await stress.prepare()
        await stress.run()
    finally:
        try:
            await stress.remove_rules()
        except (RelayGone, SetupError) as problem:
            print(f"cleaning up failed: {problem}")

        await peripheral.stop()

        if stress.results:
            stress.report()

    return 0


def main():
    address = resolve()

    try:
        return asyncio.run(run(address))
    except SetupError as problem:
        print(f"stopped: {problem}")
    except RelayGone as problem:
        print(f"stopped, the relay does not answer: {problem}")
    except KeyboardInterrupt:
        print("stopped")

    return 2


if __name__ == "__main__":
    sys.exit(main())
