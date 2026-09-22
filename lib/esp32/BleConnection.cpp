#include "BleConnection.h"

#include <algorithm>
#include <new>
#include <string>

#include <NimBLEDevice.h>

#include "BleScanner.h"
#include "HexText.h"
#include "Logger.h"
#include "NimBleConvert.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "ble";

// the Client Characteristic Configuration descriptor
constexpr uint16_t kCccdUuid = 0x2902;

// the CCCD's values, little-endian: notifications on, indications on
constexpr uint8_t kNotifyOn[2] = {0x01, 0x00};

constexpr uint8_t kIndicateOn[2] = {0x02, 0x00};

std::string formatTarget(const TargetConfig& target)
{
	return target.address() ? std::string(target.address()->text().c_str()) : "\"" + std::string(target.name().c_str()) + "\"";
}

// the most bytes of an RX frame the log shows
constexpr size_t kHexdumpBytes = 64;

// per log line; as hex beside the rule id and kLinePrefixChars they fit Logger::kMaxLine
constexpr size_t kHexdumpLineBytes = 32;

// a line's time stamp, level, tag, "relay ", " RX 512 B: " and " ...", all but the rule id
constexpr size_t kLinePrefixChars = 60;

static_assert(kHexdumpLineBytes * 3 + RuleConfig::Id::kCapacity + kLinePrefixChars <= Logger::kMaxLine, "a hexdump line fits a log line");

std::string formatHex(const uint8_t* data, size_t size)
{
	std::string text(size * 3, ' ');

	for (size_t i = 0; i < size; ++i)
	{
		writeHexByte(data[i], &text[i * 3]);
	}

	if (!text.empty())
	{
		text.pop_back();
	}

	return text;
}

}  // namespace

void BleConnection::init(BleScanner& bleScanner, uint8_t addressType)
{
	scanner = &bleScanner;
	ownAddressType = addressType;
}

bool BleConnection::isFree() const
{
	return !deviceRules && state == State::Idle;
}

bool BleConnection::isClosing() const
{
	return !deviceRules && state != State::Idle;
}

void BleConnection::setDevice(const DeviceRules& device)
{
	deviceRules = device;

	{
		LockGuard guard(&lock);
		rules.clear();
		rules.reserve(device.rules().size());

		for (const RuleConfig& rule : device.rules())
		{
			rules.push_back(Rule{.config = rule});
		}
	}

	for (Rule& rule : rules)
	{
		startRelay(rule);
	}

	foundDevice.reset();
	failedAttempts = 0;
	lastError = "";
	wasUp = false;
	rssi = 0;
	tx = TxWrite();
	nextTxRule = 0;
	state = State::Idle;
}

void BleConnection::clearDevice()
{
	for (Rule& rule : rules)
	{
		rule.server.reset();
	}

	deviceRules.reset();
	disconnect(false);
}

void BleConnection::update(uint32_t dtMs)
{
	handleGapEvents();
	updateIdleTimes(dtMs);

	if (!isLinkWanted() && state != State::Idle && state != State::Disconnecting)
	{
		disconnect(false);
	}

	updateState(dtMs);
	updateServers(dtMs);
	writeTx();
	logRxFrames();
	readRssi(dtMs);
}

bool BleConnection::readStatus(const RuleConfig::Id& id, std::optional<RelayStatus>& status) const
{
	if (!deviceRules)
	{
		return false;
	}

	for (const Rule& rule : rules)
	{
		if (!(rule.config.id() == id))
		{
			continue;
		}

		const bool up = state == State::Up;
		const std::optional<BleAddress> address = foundDevice ? std::optional<BleAddress>(foundDevice->address()) : deviceRules->target().address();
		const LinkState shown = up && !rule.error.empty() ? LinkState::Error : linkState();
		const TcpStatus tcp = rule.server ? rule.server->status() : TcpStatus();
		LockGuard guard(&lock);
		status = RelayStatus(rule.config.id(), rule.config.name(), shown, address, connected ? mtu : 0, rssi, up ? upMs / 1000 : 0,
			rule.error.empty() ? lastError : rule.error, rule.counters, tcp);
		return true;
	}

	return false;
}

int BleConnection::onGapEvent(ble_gap_event* event, void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);

	switch (event->type)
	{
		case BLE_GAP_EVENT_CONNECT:
		{
			LockGuard guard(&self->lock);
			self->connectEvent = true;
			self->connectStatus = event->connect.status;
			self->eventConnHandle = event->connect.conn_handle;
			return 0;
		}
		case BLE_GAP_EVENT_DISCONNECT:
		{
			LockGuard guard(&self->lock);
			self->disconnectEvent = true;
			self->disconnectReason = event->disconnect.reason;
			self->disconnectConnHandle = event->disconnect.conn.conn_handle;
			return 0;
		}
		case BLE_GAP_EVENT_MTU:
		{
			// whichever side asked for the exchange
			LockGuard guard(&self->lock);

			if (self->connected && event->mtu.conn_handle == self->connHandle && event->mtu.channel_id == BLE_L2CAP_CID_ATT)
			{
				self->mtu = event->mtu.value;
			}

			return 0;
		}
		case BLE_GAP_EVENT_NOTIFY_RX:
		{
			// NimBLE runs every callback on its one host task, so one static buffer is enough and keeps it off the stack
			static uint8_t data[kMaxAttributeValue];
			uint16_t size = 0;
			ble_hs_mbuf_to_flat(event->notify_rx.om, data, sizeof(data), &size);
			LockGuard guard(&self->lock);
			self->queueRx(event->notify_rx.attr_handle, data, size);
			return 0;
		}
		default:
		{
			// a connection parameter request too: 0 accepts it
			return 0;
		}
	}
}

int BleConnection::onMtu(uint16_t connHandle, const ble_gatt_error* error, uint16_t value, void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);
	LockGuard guard(&self->lock);

	if (self->connected && connHandle == self->connHandle)
	{
		self->mtu = error->status == 0 ? value : self->mtu;
		self->setRequestResult(error->status);
	}

	return 0;
}

int BleConnection::onService(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);
	LockGuard guard(&self->lock);

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (error->status == 0 && service != nullptr)
	{
		self->services.push_back(
			Service
			{
				.uuid = toBleUuid(NimBLEUUID(service->uuid)),
				.start = service->start_handle,
				.end = service->end_handle
			});
		return 0;
	}

	self->setRequestResult(error->status);

	return 0;
}

int BleConnection::onCharacteristic(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_chr* characteristic, void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);
	LockGuard guard(&self->lock);

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (error->status == 0 && characteristic != nullptr)
	{
		self->serviceCharacteristics.push_back(
			NimBleCharacteristic
			{
				.uuid = toBleUuid(NimBLEUUID(characteristic->uuid)),
				.properties = characteristic->properties,
				.declarationHandle = characteristic->def_handle,
				.valueHandle = characteristic->val_handle
			});
		return 0;
	}

	self->setRequestResult(error->status);

	return 0;
}

int BleConnection::onDescriptor(uint16_t connHandle, const ble_gatt_error* error, uint16_t /*valueHandle*/, const ble_gatt_dsc* descriptor,
	void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);
	LockGuard guard(&self->lock);

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (error->status == 0 && descriptor != nullptr)
	{
		if (descriptor->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&descriptor->uuid.u) == kCccdUuid)
		{
			self->foundCccd = descriptor->handle;
		}

		return 0;
	}

	self->setRequestResult(error->status);

	return 0;
}

int BleConnection::onWrite(uint16_t connHandle, const ble_gatt_error* error, ble_gatt_attr* /*attr*/, void* arg)
{
	BleConnection* self = static_cast<BleConnection*>(arg);
	LockGuard guard(&self->lock);

	if (self->connected && connHandle == self->connHandle)
	{
		self->setRequestResult(error->status);
	}

	return 0;
}

void BleConnection::setRequestResult(int status)
{
	requestState = status == 0 || status == BLE_HS_EDONE ? RequestState::Done : RequestState::Failed;
	requestError = status == BLE_HS_EDONE ? 0 : status;
}

void BleConnection::queueRx(uint16_t attrHandle, const uint8_t* data, size_t size)
{
	for (Rule& rule : rules)
	{
		if (!rule.receiving || !rule.ring || rule.handles.rxHandle() != attrHandle)
		{
			continue;
		}

		rule.counters.countRx(size);

		if (!rule.ring->push(data, size))
		{
			rule.counters.countRxDropped(1);
		}
	}
}

void BleConnection::handleGapEvents()
{
	bool connectHappened = false;
	bool disconnectHappened = false;
	int status = 0;
	int reason = 0;
	uint16_t newConnHandle = 0;
	uint16_t oldConnHandle = 0;

	{
		LockGuard guard(&lock);
		connectHappened = connectEvent;
		disconnectHappened = disconnectEvent;
		status = connectStatus;
		reason = disconnectReason;
		newConnHandle = eventConnHandle;
		oldConnHandle = disconnectConnHandle;
		connectEvent = false;
		disconnectEvent = false;
	}

	if (connectHappened)
	{
		onConnected(status, newConnHandle);
	}

	if (disconnectHappened)
	{
		onDisconnected(reason, oldConnHandle);
	}
}

void BleConnection::updateState(uint32_t dtMs)
{
	int error = 0;

	switch (state)
	{
		case State::Idle:
		{
			if (isLinkWanted())
			{
				startSearch();
			}

			break;
		}
		case State::Searching:
		{
			checkSearch();
			break;
		}
		case State::ExchangingMtu:
		{
			// a device that refuses the exchange keeps the default MTU, 23
			if (checkRequest("MTU exchange", error))
			{
				discoverServices();
			}

			break;
		}
		case State::DiscoveringServices:
		case State::DiscoveringCharacteristics:
		{
			if (!checkRequest("service discovery", error))
			{
				break;
			}

			if (error != 0)
			{
				fail("service discovery failed, NimBLE error " + describeNimBleError(error));
				break;
			}

			discoverNextService();
			break;
		}
		case State::DiscoveringCccds:
		{
			// a missing CCCD fails the rule, not the connection
			if (checkRequest("CCCD discovery", error))
			{
				discoverNextCccd();
			}

			break;
		}
		case State::Subscribing:
		{
			if (checkRequest("CCCD write", error))
			{
				subscribeNext();
			}

			break;
		}
		case State::Up:
		{
			upMs += dtMs;
			break;
		}
		case State::Waiting:
		{
			waitedMs += dtMs;
			state = waitedMs >= retryWaitMs ? State::Idle : State::Waiting;
			break;
		}
		case State::Connecting:
		{
			// the connect or disconnect event changes the state
			break;
		}
		case State::Disconnecting:
		{
			waitForDisconnect(dtMs);
			break;
		}
	}
}

void BleConnection::startSearch()
{
	searchSlot = scanner->startSearch(deviceRules->target());

	if (searchSlot >= BleScanner::kMaxSearches)
	{
		fail("no free search slot in the scanner");
		return;
	}

	state = State::Searching;
	Logger::info(kTag, "BLE link to %s: searching", formatTarget(deviceRules->target()).c_str());
}

// Asks for the radio once the device is found; the search keeps its result while a scan or another connect has the radio.
void BleConnection::checkSearch()
{
	std::optional<BleDeviceBase> device;
	bool duplicateName = false;
	const BleScanner::SearchState search = scanner->readSearch(searchSlot, device, duplicateName);

	if (search == BleScanner::SearchState::Searching || (search == BleScanner::SearchState::Found && !scanner->acquireRadio(this)))
	{
		return;
	}

	scanner->stopSearch(searchSlot);

	if (search != BleScanner::SearchState::Found || !device)
	{
		fail("not found within " + std::to_string(BleScanner::kSearchTimeoutMs / 1000) + " s of scanning");
		return;
	}

	foundDevice = device;

	if (duplicateName)
	{
		Logger::warn(kTag, "BLE link to %s: more than one device has this name; using the strongest, %s, %d dBm",
			formatTarget(deviceRules->target()).c_str(), foundDevice->address().text().c_str(), foundDevice->rssi());
	}

	connect();
}

void BleConnection::connect()
{
	const NimBLEAddress address = toNimBleAddress(foundDevice->address(), foundDevice->addressType());
	const int started = ble_gap_connect(ownAddressType, address.getBase(), kConnectTimeoutMs, nullptr, onGapEvent, this);

	if (started != 0)
	{
		fail("the connect did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	connectPending = true;
	state = State::Connecting;
	Logger::info(kTag, "BLE link to %s: found at %d dBm, connecting", foundDevice->address().text().c_str(), foundDevice->rssi());
}

void BleConnection::onConnected(int status, uint16_t newConnHandle)
{
	// the event of a connect given up after kCloseWaitMs: a connection made then closes at once, and its disconnect event is ignored
	if (!connectPending)
	{
		if (status == 0)
		{
			ble_gap_terminate(newConnHandle, BLE_ERR_REM_USER_CONN_TERM);
		}

		return;
	}

	connectPending = false;
	scanner->releaseRadio(this);

	if (status == 0)
	{
		LockGuard guard(&lock);
		connHandle = newConnHandle;
		connected = true;
		mtu = BLE_ATT_MTU_DFLT;
	}

	// a disconnect was requested during the connect: close the new connection now; its disconnect event finishes the disconnect
	if (state == State::Disconnecting && status == 0)
	{
		waitedMs = 0;

		if (ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM) != 0)
		{
			finishDisconnect();
		}

		return;
	}

	if (state == State::Disconnecting)
	{
		finishDisconnect();
		return;
	}

	if (status != 0)
	{
		fail(status == BLE_HS_ETIMEOUT ? "no connection within " + std::to_string(kConnectTimeoutMs / 1000) + " s"
			: "no connection, NimBLE error " + describeNimBleError(status));
		return;
	}

	Logger::info(kTag, "BLE link to %s: connected", foundDevice->address().text().c_str());
	setRequestState(RequestState::Running);

	// the device may have exchanged the MTU first, and NimBLE starts no second exchange: the link has its MTU already
	if (ble_gattc_exchange_mtu(connHandle, onMtu, this) != 0)
	{
		const uint16_t linkMtu = ble_att_mtu(connHandle);

		{
			LockGuard guard(&lock);
			mtu = linkMtu != 0 ? linkMtu : BLE_ATT_MTU_DFLT;
		}

		setRequestState(RequestState::Done);
	}

	state = State::ExchangingMtu;
}

void BleConnection::onDisconnected(int reason, uint16_t oldConnHandle)
{
	// NimBLE reports a connect only once it has read the device's features; a link that fails before that ends with this event alone
	const bool connecting = connectPending;

	{
		LockGuard guard(&lock);

		// the event of a connection given up after kCloseWaitMs
		if (!connecting && (!connected || oldConnHandle != connHandle))
		{
			return;
		}

		connected = false;
	}

	connectPending = false;

	if (state == State::Disconnecting)
	{
		finishDisconnect();
		return;
	}

	fail((connecting ? "no connection, reason " : "the connection was lost, reason ") + describeNimBleError(reason));
}

void BleConnection::discoverServices()
{
	{
		LockGuard guard(&lock);
		services.clear();
		requestState = RequestState::Running;
	}

	const int started = ble_gattc_disc_all_svcs(connHandle, onService, this);

	if (started != 0)
	{
		fail("service discovery did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	index = 0;
	state = State::DiscoveringServices;
}

void BleConnection::discoverNextService()
{
	if (state == State::DiscoveringCharacteristics)
	{
		LockGuard guard(&lock);
		Service& service = services[index++];
		const std::vector<NimBleCharacteristic>& found = serviceCharacteristics;

		for (size_t i = 0; i < found.size(); ++i)
		{
			const uint16_t end = i + 1 < found.size() ? found[i + 1].declarationHandle - 1 : service.end;
			service.characteristics.push_back(DiscoveredCharacteristic(found[i].uuid, found[i].properties, found[i].valueHandle, end));
		}
	}

	while (index < services.size() && !isRuleService(services[index]))
	{
		++index;
	}

	if (index == services.size())
	{
		findChannels();
		return;
	}

	setRequestState(RequestState::Running);
	{
		LockGuard guard(&lock);
		serviceCharacteristics.clear();
	}

	const int started = ble_gattc_disc_all_chrs(connHandle, services[index].start, services[index].end, onCharacteristic, this);

	if (started != 0)
	{
		fail("characteristic discovery did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	state = State::DiscoveringCharacteristics;
}

bool BleConnection::isRuleService(const Service& service) const
{
	return std::any_of(rules.begin(), rules.end(),
		[&service](const Rule& rule)
		{
			return rule.config.serviceUuid().equals(service.uuid);
		});
}

// A rule the device lacks something for stays down and the others run; when no rule can run, the attempt fails.
void BleConnection::findChannels()
{
	size_t usable = 0;

	for (Rule& rule : rules)
	{
		std::string error;
		auto service = std::find_if(services.begin(), services.end(),
			[&rule](const Service& candidate)
			{
				return rule.config.serviceUuid().equals(candidate.uuid);
			});

		if (service == services.end())
		{
			error = std::string("service ") + rule.config.serviceUuid().text().c_str() + ": not on the device";
		}
		else if (ChannelHandles::find(rule.config, service->characteristics, rule.handles, error))
		{
			++usable;
		}

		rule.error = error.c_str();
	}

	if (usable == 0)
	{
		fail(rules.front().error.c_str());
		return;
	}

	// before the subscriptions: a value may arrive right after its CCCD is written
	{
		LockGuard guard(&lock);

		for (Rule& rule : rules)
		{
			rule.receiving = rule.error.empty() && rule.handles.rxHandle() != 0;
		}
	}

	index = 0;
	discoverNextCccd();
}

void BleConnection::discoverNextCccd()
{
	if (state == State::DiscoveringCccds)
	{
		LockGuard guard(&lock);
		Rule& rule = rules[index++];
		rule.cccd = foundCccd;
		rule.error = foundCccd != 0 ? "" : (std::string("rx ") + rule.config.rx().uuid().text().c_str() + ": no CCCD to subscribe with").c_str();
	}

	while (index < rules.size() && (!rules[index].error.empty() || rules[index].handles.rxHandle() == 0))
	{
		++index;
	}

	if (index == rules.size())
	{
		index = 0;
		subscribeNext();
		return;
	}

	const ChannelHandles& handles = rules[index].handles;
	setRequestState(RequestState::Running);
	{
		LockGuard guard(&lock);
		foundCccd = 0;
	}

	// a characteristic with no handle after its value has no descriptors: the request ends at once with none
	if (handles.rxEnd() <= handles.rxHandle() || ble_gattc_disc_all_dscs(connHandle, handles.rxHandle(), handles.rxEnd(), onDescriptor, this) != 0)
	{
		setRequestState(RequestState::Done);
	}

	state = State::DiscoveringCccds;
}

// Writing the CCCD turns on indications or notifications: the only state change the relay makes on the device before TCP writes.
void BleConnection::subscribeNext()
{
	int error = 0;

	if (state == State::Subscribing)
	{
		LockGuard guard(&lock);
		error = requestError;
		Rule& rule = rules[index++];
		rule.error = error == 0 ? "" : (std::string("rx ") + rule.config.rx().uuid().text().c_str() + ": subscribing failed, NimBLE error " +
			describeNimBleError(error)).c_str();
	}

	while (index < rules.size() && (!rules[index].error.empty() || rules[index].handles.rxHandle() == 0))
	{
		++index;
	}

	if (index == rules.size())
	{
		finishSetup();
		return;
	}

	const Rule& rule = rules[index];
	setRequestState(RequestState::Running);

	if (ble_gattc_write_flat(connHandle, rule.cccd, rule.handles.rxIndicate() ? kIndicateOn : kNotifyOn, sizeof(kNotifyOn), onWrite, this) != 0)
	{
		LockGuard guard(&lock);
		requestState = RequestState::Failed;
		requestError = BLE_HS_EUNKNOWN;
	}

	state = State::Subscribing;
}

void BleConnection::finishSetup()
{
	failedAttempts = 0;
	lastError = "";
	upMs = 0;
	sinceRssiMs = kRssiPeriodMs;
	state = State::Up;

	for (Rule& rule : rules)
	{
		if (wasUp)
		{
			rule.counters.countReconnect();
		}

		Logger::info(kTag, "relay %s on %s: %s; TX 0x%04x%s, RX 0x%04x %s", rule.config.id().c_str(), foundDevice->address().text().c_str(),
			rule.error.empty() ? "up" : rule.error.c_str(), rule.handles.txHandle(), rule.handles.txWithResponse() ? " with response" : "",
			rule.handles.rxHandle(), rule.handles.rxIndicate() ? "indicate" : "notify");
	}

	wasUp = true;
}

void BleConnection::fail(const std::string& why)
{
	lastError = why.c_str();
	++failedAttempts;
	const std::string device = foundDevice ? std::string(foundDevice->address().text().c_str())
		: deviceRules ? formatTarget(deviceRules->target()) : "?";
	Logger::warn(kTag, "BLE link to %s: %s", device.c_str(), why.c_str());

	const bool lost = state == State::Up;
	// the clients that never saw the link wait through the attempts until the next wait is the backoff's last
	const bool givingUp = deviceRules && failedAttempts >= deviceRules->backoffSteps();
	const std::string attempts = "the BLE connection failed " + std::to_string(failedAttempts) + " times";

	for (Rule& rule : rules)
	{
		if (rule.server && lost && rule.config.closeTcpOnBleLoss())
		{
			rule.server->closeClients("the BLE link was lost");
		}
		else if (rule.server && !lost && givingUp)
		{
			rule.server->closeWaitingClients(attempts.c_str());
		}
	}

	disconnect(true);
}

void BleConnection::disconnect(bool retry)
{
	retryAfterDisconnect = retry;

	switch (state)
	{
		case State::Idle:
		case State::Waiting:
		{
			finishDisconnect();
			return;
		}
		case State::Searching:
		{
			scanner->stopSearch(searchSlot);
			finishDisconnect();
			return;
		}
		case State::Connecting:
		{
			// with a connect running its event follows, the connection made or cancelled; without one, nothing follows
			if (!connectPending)
			{
				finishDisconnect();
				return;
			}

			ble_gap_conn_cancel();
			waitedMs = 0;
			state = State::Disconnecting;
			return;
		}
		case State::Disconnecting:
		{
			return;
		}
		case State::ExchangingMtu:
		case State::DiscoveringServices:
		case State::DiscoveringCharacteristics:
		case State::DiscoveringCccds:
		case State::Subscribing:
		case State::Up:
		{
			waitedMs = 0;
			state = State::Disconnecting;

			if (ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM) != 0)
			{
				finishDisconnect();
			}

			return;
		}
	}
}

void BleConnection::waitForDisconnect(uint32_t dtMs)
{
	waitedMs += dtMs;

	if (waitedMs < kCloseWaitMs)
	{
		return;
	}

	const std::string device = foundDevice ? std::string(foundDevice->address().text().c_str()) : "?";
	Logger::warn(kTag, "BLE link to %s: the connection did not close within %u s; taken as closed", device.c_str(),
		(unsigned)(kCloseWaitMs / 1000));
	finishDisconnect();
}

void BleConnection::finishDisconnect()
{
	const bool hadLink = state != State::Idle && state != State::Searching && state != State::Waiting;
	// all the connection holds goes; a late connect event of NimBLE's then counts as given up
	connectPending = false;
	scanner->releaseRadio(this);

	if (tx.running)
	{
		finishTx(false);
	}

	{
		LockGuard guard(&lock);
		connected = false;

		for (Rule& rule : rules)
		{
			rule.receiving = false;
		}
	}

	mtu = 0;
	upMs = 0;

	if (retryAfterDisconnect && deviceRules)
	{
		retryWaitMs = deviceRules->backoffMs(failedAttempts > 0 ? failedAttempts - 1 : 0);
		waitedMs = 0;
		state = State::Waiting;
		Logger::info(kTag, "BLE link to %s: next attempt in %u s", formatTarget(deviceRules->target()).c_str(), (unsigned)(retryWaitMs / 1000));
	}
	else
	{
		state = State::Idle;

		if (hadLink && foundDevice)
		{
			Logger::info(kTag, "BLE link to %s: disconnected", foundDevice->address().text().c_str());
		}
	}

	if (!deviceRules)
	{
		LockGuard guard(&lock);
		rules.clear();
		foundDevice.reset();
	}
}

void BleConnection::startRelay(Rule& rule)
{
	if (rule.config.rx().mode() != ChannelMode::None)
	{
		rule.ring.reset(new (std::nothrow) TcpServer::RxRing());

		if (rule.ring)
		{
			rule.logReader = rule.ring->createReader();
		}
		else
		{
			Logger::error(kTag, "relay %s: no memory for its RX queue; its clients get nothing", rule.config.id().c_str());
		}
	}

	rule.server.reset(new (std::nothrow) TcpServer(rule.config, rule.ring.get(), lock, rule.counters));

	if (!rule.server)
	{
		Logger::error(kTag, "relay %s: no memory for its TCP server", rule.config.id().c_str());
	}
}

bool BleConnection::isLinkWanted() const
{
	return deviceRules && std::any_of(rules.begin(), rules.end(),
		[](const Rule& rule)
		{
			return rule.config.connectPolicy() == RuleConfig::ConnectPolicy::Always || rule.holdLink;
		});
}

void BleConnection::updateIdleTimes(uint32_t dtMs)
{
	for (Rule& rule : rules)
	{
		if (rule.server && rule.server->clientCount() > 0)
		{
			rule.holdLink = true;
			rule.idleMs = 0;
		}
		else if (rule.holdLink)
		{
			rule.idleMs += dtMs;
			rule.holdLink = rule.idleMs < rule.config.idleS() * 1000u;
		}
	}
}

void BleConnection::updateServers(uint32_t dtMs)
{
	const bool up = state == State::Up;
	const uint16_t linkMtu = currentMtu();

	for (Rule& rule : rules)
	{
		if (rule.server)
		{
			rule.server->update(dtMs, up && rule.error.empty(), linkMtu);
		}
	}
}

void BleConnection::writeTx()
{
	if (state != State::Up)
	{
		return;
	}

	if (tx.running)
	{
		continueTx();
	}

	if (!tx.running)
	{
		startTx();
	}
}

// A rule the device lacks its TX for, and a packet no write takes, lose their packets, counted.
void BleConnection::startTx()
{
	const uint16_t linkMtu = currentMtu();

	for (size_t i = 0; i < rules.size(); ++i)
	{
		const size_t index = (nextTxRule + i) % rules.size();
		Rule& rule = rules[index];
		uint32_t client = 0;
		TcpClientData* data = rule.server ? rule.server->nextPacket(client) : nullptr;

		if (data == nullptr)
		{
			continue;
		}

		nextTxRule = (index + 1) % rules.size();
		const size_t size = data->peek(nullptr, 0, 0);
		tx = TxWrite
		{
			.running = true,
			.rule = index,
			.client = client,
			.mode = chooseWriteMode(size, linkMtu, rule.config.oversize()),
			.size = size
		};

		if (tx.mode == WriteMode::Drop)
		{
			rule.counters.countOversize();
			Logger::warn(kTag, "relay %s: a packet of %u B dropped: longer than a write takes at MTU %u", rule.config.id().c_str(), (unsigned)size,
				(unsigned)linkMtu);
		}

		if (tx.mode == WriteMode::Drop || !rule.error.empty() || rule.handles.txHandle() == 0)
		{
			finishTx(false);
			continue;
		}

		continueTx();
		return;
	}
}

// Writes the TCP data of a client to the device's TX characteristic: the relay's one state change on the device besides the CCCDs.
void BleConnection::sendTx()
{
	Rule& rule = rules[tx.rule];
	TcpClientData* data = rule.server ? rule.server->clientData(tx.client) : nullptr;

	// the client left: its data went with it
	if (data == nullptr)
	{
		tx = TxWrite();
		return;
	}

	uint8_t bytes[kMaxAttributeValue];
	const uint16_t handle = rule.handles.txHandle();
	const bool longWrite = tx.mode == WriteMode::LongWrite;
	int result = 0;

	tx.piece = longWrite ? tx.size : std::min(tx.size - tx.offset, maxWriteBytes(currentMtu()));

	// no MTU, no write: the loop in continueTx() would never end
	if (tx.piece == 0)
	{
		finishTx(false);
		return;
	}

	data->peek(bytes, tx.piece, tx.offset);

	if (longWrite || rule.handles.txWithResponse())
	{
		os_mbuf* value = longWrite ? ble_hs_mbuf_from_flat(bytes, tx.piece) : nullptr;
		setRequestState(RequestState::Running);
		result = longWrite ? (value != nullptr ? ble_gattc_write_long(connHandle, handle, 0, value, onWrite, this) : BLE_HS_ENOMEM)
			: ble_gattc_write_flat(connHandle, handle, bytes, tx.piece, onWrite, this);
		tx.waiting = result == 0;
	}
	else if ((result = ble_gattc_write_no_rsp_flat(connHandle, handle, bytes, tx.piece)) == 0)
	{
		rule.counters.countTxWrite();
		tx.offset += tx.piece;
	}

	// NimBLE out of buffers: the same write again at the next update
	if (result != 0 && result != BLE_HS_ENOMEM && result != BLE_HS_EBUSY)
	{
		Logger::warn(kTag, "relay %s: a TX write did not start, NimBLE error %s; the packet is dropped", rule.config.id().c_str(),
			describeNimBleError(result).c_str());
		finishTx(false);
	}
}

void BleConnection::continueTx()
{
	if (tx.waiting)
	{
		int error = 0;

		if (!checkRequest("TX write", error))
		{
			return;
		}

		tx.waiting = false;

		if (error != 0)
		{
			Logger::warn(kTag, "relay %s: a TX write failed, NimBLE error %s; the packet is dropped", rules[tx.rule].config.id().c_str(),
				describeNimBleError(error).c_str());
			finishTx(false);
			return;
		}

		rules[tx.rule].counters.countTxWrite();
		tx.offset += tx.piece;
	}

	// writes without response go out back to back; one that NimBLE has no buffer for waits for the next update
	while (tx.running && !tx.waiting && tx.offset < tx.size)
	{
		const size_t before = tx.offset;
		sendTx();

		if (tx.running && !tx.waiting && tx.offset == before)
		{
			return;
		}
	}

	if (tx.running && !tx.waiting)
	{
		finishTx(true);
	}
}

void BleConnection::finishTx(bool written)
{
	Rule& rule = rules[tx.rule];
	TcpClientData* data = rule.server ? rule.server->clientData(tx.client) : nullptr;

	if (data != nullptr)
	{
		if (!written)
		{
			rule.counters.countTxDropped(tx.size - tx.offset);
		}

		data->pop();
	}

	tx = TxWrite();
}

void BleConnection::logRxFrames()
{
	uint8_t frame[kMaxAttributeValue];

	for (Rule& rule : rules)
	{
		if (!rule.ring || !rule.config.hexdump())
		{
			continue;
		}

		for (size_t size = readLogFrame(rule, frame, sizeof(frame)); size > 0; size = readLogFrame(rule, frame, sizeof(frame)))
		{
			logHexdump(rule, frame, size);
		}
	}
}

// "relay id0 RX 57 B: 01 03 ...", then "relay id0 RX +32: ..." for the next line; " ..." ends it when the frame is longer
void BleConnection::logHexdump(const Rule& rule, const uint8_t* frame, size_t size) const
{
	const size_t shown = std::min(size, kHexdumpBytes);

	for (size_t at = 0; at < shown; at += kHexdumpLineBytes)
	{
		const size_t count = std::min(kHexdumpLineBytes, shown - at);
		const char* more = at + count == shown && shown < size ? " ..." : "";
		const std::string hex = formatHex(frame + at, count);

		if (at == 0)
		{
			Logger::info(kTag, "relay %s RX %u B: %s%s", rule.config.id().c_str(), (unsigned)size, hex.c_str(), more);
		}
		else
		{
			Logger::info(kTag, "relay %s RX +%u: %s%s", rule.config.id().c_str(), (unsigned)at, hex.c_str(), more);
		}
	}
}

size_t BleConnection::readLogFrame(Rule& rule, uint8_t* out, size_t room)
{
	LockGuard guard(&lock);

	return rule.ring->read(rule.logReader, out, room);
}

void BleConnection::readRssi(uint32_t dtMs)
{
	if (state != State::Up)
	{
		return;
	}

	sinceRssiMs += dtMs;

	if (sinceRssiMs < kRssiPeriodMs)
	{
		return;
	}

	sinceRssiMs = 0;
	int8_t value = 0;

	if (ble_gap_conn_rssi(connHandle, &value) == 0)
	{
		rssi = value;
	}
}

bool BleConnection::checkRequest(const char* what, int& error)
{
	LockGuard guard(&lock);

	if (requestState == RequestState::Running || requestState == RequestState::None)
	{
		return false;
	}

	error = requestState == RequestState::Failed ? requestError : 0;
	requestState = RequestState::None;

	if (error != 0)
	{
		Logger::debug(kTag, "BLE link: %s: NimBLE error %d", what, error);
	}

	return true;
}

void BleConnection::setRequestState(RequestState next)
{
	LockGuard guard(&lock);
	requestState = next;
	requestError = 0;
}

uint16_t BleConnection::currentMtu() const
{
	LockGuard guard(&lock);

	return mtu;
}

LinkState BleConnection::linkState() const
{
	switch (state)
	{
		case State::Idle:                       return LinkState::Idle;
		case State::Searching:                  return LinkState::Searching;
		case State::Connecting:
		case State::ExchangingMtu:              return LinkState::Connecting;
		case State::DiscoveringServices:
		case State::DiscoveringCharacteristics:
		case State::DiscoveringCccds:           return LinkState::Discovering;
		case State::Subscribing:                return LinkState::Subscribing;
		case State::Up:                         return LinkState::Up;
		case State::Disconnecting:              return LinkState::Disconnecting;
		case State::Waiting:                    return LinkState::Waiting;
	}

	return LinkState::Idle;
}

}  // namespace w2b
