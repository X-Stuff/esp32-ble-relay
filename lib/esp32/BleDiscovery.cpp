#include "BleDiscovery.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include <NimBLEDevice.h>

#include "BleConnection.h"
#include "BleScanner.h"
#include "Logger.h"
#include "NimBleConvert.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "ble";

constexpr uint16_t kGapService = 0x1800;

constexpr uint16_t kDeviceInformationService = 0x180a;

// GAP's name and appearance; Device Information's manufacturer, model, serial number, hardware, firmware, software, PnP ID
constexpr uint16_t kInfoCharacteristics[] = {0x2a00, 0x2a01, 0x2a29, 0x2a24, 0x2a25, 0x2a27, 0x2a26, 0x2a28, 0x2a50};

BleUuid makeUuid16(uint16_t value)
{
	const uint8_t bytes[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xff)};

	return BleUuid(bytes, sizeof(bytes));
}

bool isInfoService(const BleUuid& uuid)
{
	return uuid.equals(makeUuid16(kGapService)) || uuid.equals(makeUuid16(kDeviceInformationService));
}

bool isInfoCharacteristic(const BleUuid& uuid)
{
	return std::any_of(std::begin(kInfoCharacteristics), std::end(kInfoCharacteristics),
		[&uuid](uint16_t info)
		{
			return uuid.equals(makeUuid16(info));
		});
}

}  // namespace

const char* BleDiscovery::stateStr(State state)
{
	switch (state)
	{
		case State::Idle:                       return "idle";
		case State::Requested:                  return "requested";
		case State::Connecting:                 return "connecting";
		case State::DiscoveringServices:
		case State::DiscoveringCharacteristics:
		case State::ReadingValues:
		case State::Disconnecting:              return "reading";
		case State::Done:                       return "done";
		case State::Failed:                     return "failed";
	}

	return "?";
}

BleDiscovery::BleDiscovery(BleScanner& scanner)
	: scanner(scanner)
{
}

// The relay's own address as BleService chose it from the configuration, the one its log line names.
void BleDiscovery::setup()
{
	if (!NimBLEDevice::isInitialized())
	{
		Logger::error(kTag, "NimBLE is not running; every BLE probe fails");
		return;
	}

	ownAddressType = NimBLEDevice::getAddress().getType();
	nimBleRunning = true;
}

void BleDiscovery::update(uint32_t dtMs)
{
	handleGapEvents();
	updateState(dtMs);
}

bool BleDiscovery::request(const BleAddress& address, BleDeviceBase::AddressType type)
{
	LockGuard guard(&lock);

	if (state != State::Idle && state != State::Done && state != State::Failed)
	{
		return false;
	}

	peer = address;
	peerType = type;
	finishedTable = GattTable();
	radioWaitedMs = 0;
	state = State::Requested;

	return true;
}

BleDiscovery::State BleDiscovery::read(BleAddress& address, GattTable& found) const
{
	LockGuard guard(&lock);
	address = peer;
	found = finishedTable;

	return state;
}

BleDiscovery::State BleDiscovery::currentState() const
{
	LockGuard guard(&lock);

	return state;
}

int BleDiscovery::onGapEvent(ble_gap_event* event, void* arg)
{
	BleDiscovery* self = static_cast<BleDiscovery*>(arg);
	LockGuard guard(&self->lock);

	if (event->type == BLE_GAP_EVENT_CONNECT)
	{
		self->connectEvent = true;
		self->connectStatus = event->connect.status;
		self->eventConnHandle = event->connect.conn_handle;
	}
	else if (event->type == BLE_GAP_EVENT_DISCONNECT)
	{
		self->disconnectEvent = true;
		self->disconnectReason = event->disconnect.reason;
		self->disconnectConnHandle = event->disconnect.conn.conn_handle;
	}
	else if (event->type == BLE_GAP_EVENT_ENC_CHANGE)
	{
		self->encryptionEvent = true;
		self->encryptionStatus = event->enc_change.status;
	}

	// every event, a connection parameter request too: 0 accepts it
	return 0;
}

int BleDiscovery::onService(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg)
{
	BleDiscovery* self = static_cast<BleDiscovery*>(arg);
	LockGuard guard(&self->lock);

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (error->status == 0 && service != nullptr)
	{
		self->gattRequest.services.push_back(
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

int BleDiscovery::onCharacteristic(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_chr* characteristic, void* arg)
{
	BleDiscovery* self = static_cast<BleDiscovery*>(arg);
	LockGuard guard(&self->lock);

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (error->status == 0 && characteristic != nullptr)
	{
		self->gattRequest.characteristics.push_back(
			FoundCharacteristic
			{
				.uuid = toBleUuid(NimBLEUUID(characteristic->uuid)),
				.properties = characteristic->properties,
				.valueHandle = characteristic->val_handle
			});
		return 0;
	}

	self->setRequestResult(error->status);

	return 0;
}

int BleDiscovery::onValue(uint16_t connHandle, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg)
{
	BleDiscovery* self = static_cast<BleDiscovery*>(arg);
	LockGuard guard(&self->lock);
	GattRequest& current = self->gattRequest;
	const int status = error->status;

	if (!self->connected || connHandle != self->connHandle)
	{
		return 0;
	}

	if (status != 0)
	{
		// a device may answer the Read Blob after a value's last full part with one of these errors
		const bool longEnough = status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG) || status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET);
		self->setRequestResult(longEnough && current.valueSize > 0 ? 0 : status);
		return 0;
	}

	if (attr == nullptr || attr->om == nullptr)
	{
		self->setRequestResult(BLE_HS_EUNKNOWN);
		return 1;
	}

	uint16_t copied = 0;
	ble_hs_mbuf_to_flat(attr->om, current.value.data() + current.valueSize, current.value.size() - current.valueSize, &copied);
	current.valueSize += copied;

	if (current.valueSize < current.value.size())
	{
		return 0;
	}

	self->setRequestResult(0);

	return 1;
}

void BleDiscovery::setRequestResult(int status)
{
	gattRequest.state = status == 0 || status == BLE_HS_EDONE ? RequestState::Done : RequestState::Failed;
	gattRequest.error = status == BLE_HS_EDONE ? 0 : status;
}

void BleDiscovery::handleGapEvents()
{
	bool connectHappened = false;
	bool disconnectHappened = false;
	bool encryptionHappened = false;
	int status = 0;
	int reason = 0;
	int encryption = 0;
	uint16_t newConnHandle = 0;
	uint16_t oldConnHandle = 0;

	{
		LockGuard guard(&lock);
		connectHappened = connectEvent;
		disconnectHappened = disconnectEvent;
		encryptionHappened = encryptionEvent;
		status = connectStatus;
		reason = disconnectReason;
		encryption = encryptionStatus;
		newConnHandle = eventConnHandle;
		oldConnHandle = disconnectConnHandle;
		connectEvent = false;
		disconnectEvent = false;
		encryptionEvent = false;
	}

	if (connectHappened)
	{
		onConnected(status, newConnHandle);
	}

	if (encryptionHappened)
	{
		onEncryptionChanged(encryption);
	}

	if (disconnectHappened)
	{
		onDisconnected(reason, oldConnHandle);
	}
}

void BleDiscovery::updateState(uint32_t dtMs)
{
	const State current = currentState();
	int error = 0;

	switch (current)
	{
		case State::Requested:
		{
			startProbe(dtMs);
			break;
		}
		case State::DiscoveringServices:
		case State::DiscoveringCharacteristics:
		{
			if (!checkRequest(error))
			{
				break;
			}

			if (error != 0)
			{
				fail(std::string(current == State::DiscoveringServices ? "service" : "characteristic") + " discovery failed, NimBLE error " +
					describeNimBleError(error));
				break;
			}

			discoverNextService();
			break;
		}
		case State::ReadingValues:
		{
			if (checkRequest(error))
			{
				storeValue(error);
				readNextValue();
			}

			break;
		}
		case State::Disconnecting:
		{
			waitForDisconnect(dtMs);
			break;
		}
		case State::Idle:
		case State::Connecting:
		case State::Done:
		case State::Failed:
		{
			// Connecting: the connect or disconnect event changes the state
			break;
		}
	}
}

// One try per update(): the radio is acquired here, never waited for, so no task is held up.
void BleDiscovery::startProbe(uint32_t dtMs)
{
	// each log line before Failed, which lets request() write peer again
	if (!nimBleRunning)
	{
		Logger::warn(kTag, "BLE probe of %s: NimBLE is not running", peer.text().c_str());
		setState(State::Failed);
		return;
	}

	if (!scanner.acquireRadio(this))
	{
		radioWaitedMs += dtMs;

		if (radioWaitedMs >= BleConnection::kRadioWaitMs)
		{
			Logger::warn(kTag, "BLE probe of %s: the radio stayed busy with scans and connects for %u s", peer.text().c_str(),
				(unsigned)(BleConnection::kRadioWaitMs / 1000));
			setState(State::Failed);
		}

		return;
	}

	failed = false;
	connect();
}

// No MTU exchange: the probe needs no more than the default 23 bytes.
void BleDiscovery::connect()
{
	const NimBLEAddress address = toNimBleAddress(peer, peerType);
	Logger::info(kTag, "BLE probe of %s, a %s address: connecting", peer.text().c_str(),
		peerType == BleDeviceBase::AddressType::Random ? "random" : "public");
	const int started = ble_gap_connect(ownAddressType, address.getBase(), BleConnection::kConnectTimeoutMs, nullptr, onGapEvent, this);

	if (started != 0)
	{
		fail("the connect did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	setState(State::Connecting);
}

void BleDiscovery::onConnected(int status, uint16_t newConnHandle)
{
	scanner.releaseRadio(this);

	if (status != 0)
	{
		fail(status == BLE_HS_ETIMEOUT ? "no connection within " + std::to_string(BleConnection::kConnectTimeoutMs / 1000) + " s"
			: "no connection, NimBLE error " + describeNimBleError(status));
		return;
	}

	{
		LockGuard guard(&lock);
		connHandle = newConnHandle;
		connected = true;
	}

	Logger::info(kTag, "BLE probe of %s: connected, reading its GATT table", peer.text().c_str());
	discoverServices();
}

void BleDiscovery::onDisconnected(int reason, uint16_t oldConnHandle)
{
	// NimBLE reports a connect only once it has read the device's features; a link that fails before that ends with this event alone
	if (currentState() == State::Connecting)
	{
		fail("no connection, reason " + describeNimBleError(reason));
		return;
	}

	{
		LockGuard guard(&lock);

		// the event of a connection given up after BleConnection::kCloseWaitMs
		if (!connected || oldConnHandle != connHandle)
		{
			return;
		}

		connected = false;
	}

	if (currentState() != State::Disconnecting)
	{
		Logger::warn(kTag, "BLE probe of %s: the connection was lost while reading the GATT table, reason %s", peer.text().c_str(),
			describeNimBleError(reason).c_str());
		failed = true;
	}

	finishDisconnect();
}

// The device asked for security: NimBLE paired with Just Works, no bond; a device that wants a passkey or a bond refuses it.
void BleDiscovery::onEncryptionChanged(int status)
{
	if (status == 0)
	{
		Logger::info(kTag, "BLE probe of %s: the device asked for security; paired without a bond, the connection is encrypted", peer.text().c_str());
		return;
	}

	Logger::warn(kTag, "BLE probe of %s: the device asked for security; pairing failed, %s", peer.text().c_str(), describeNimBleError(status).c_str());
}

void BleDiscovery::discoverServices()
{
	services.clear();
	valuesToRead.clear();
	tableInProgress = GattTable();
	startRequest();
	const int started = ble_gattc_disc_all_svcs(connHandle, onService, this);

	if (started != 0)
	{
		fail("service discovery did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	setState(State::DiscoveringServices);
}

void BleDiscovery::discoverNextService()
{
	if (currentState() == State::DiscoveringServices)
	{
		LockGuard guard(&lock);
		services = std::move(gattRequest.services);
		index = 0;
	}
	else
	{
		addCharacteristics();
	}

	if (index == services.size())
	{
		index = 0;
		readNextValue();
		return;
	}

	startRequest();
	const int started = ble_gattc_disc_all_chrs(connHandle, services[index].start, services[index].end, onCharacteristic, this);

	if (started != 0)
	{
		fail("characteristic discovery did not start, NimBLE error " + describeNimBleError(started));
		return;
	}

	setState(State::DiscoveringCharacteristics);
}

void BleDiscovery::addCharacteristics()
{
	const Service& service = services[index++];
	GattService entry(service.uuid);
	LockGuard guard(&lock);

	for (const FoundCharacteristic& characteristic : gattRequest.characteristics)
	{
		entry.add(GattCharacteristic(characteristic.uuid, characteristic.properties));

		if (isInfoService(service.uuid) && isInfoCharacteristic(characteristic.uuid) && (characteristic.properties & BLE_GATT_CHR_PROP_READ) != 0)
		{
			valuesToRead.push_back(ValueToRead{.uuid = characteristic.uuid, .handle = characteristic.valueHandle});
		}
	}

	tableInProgress.add(entry);
}

// A plain Read, then Read Blobs for a longer value; on a security error the value is left out, never paired for.
void BleDiscovery::readNextValue()
{
	for (; index < valuesToRead.size(); ++index)
	{
		startRequest();
		const int started = ble_gattc_read_long(connHandle, valuesToRead[index].handle, 0, onValue, this);

		if (started == 0)
		{
			setState(State::ReadingValues);
			return;
		}

		Logger::info(kTag, "BLE probe of %s: %s not read, NimBLE error %d", peer.text().c_str(), valuesToRead[index].uuid.text().c_str(), started);
	}

	disconnect();
}

void BleDiscovery::storeValue(int error)
{
	const ValueToRead& value = valuesToRead[index++];

	if (error != 0)
	{
		Logger::info(kTag, "BLE probe of %s: %s not read, NimBLE error %s", peer.text().c_str(), value.uuid.text().c_str(),
			describeNimBleError(error).c_str());
		return;
	}

	LockGuard guard(&lock);
	tableInProgress.add(GattValue(value.uuid, gattRequest.value.data(), gattRequest.valueSize));
}

void BleDiscovery::fail(const std::string& why)
{
	Logger::warn(kTag, "BLE probe of %s: %s", peer.text().c_str(), why.c_str());
	failed = true;
	disconnect();
}

void BleDiscovery::disconnect()
{
	if (!isConnected())
	{
		finishDisconnect();
		return;
	}

	closeWaitedMs = 0;
	setState(State::Disconnecting);
	const int result = ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);

	// not connected: the disconnect event is on its way
	if (result != 0 && result != BLE_HS_ENOTCONN)
	{
		Logger::warn(kTag, "BLE probe of %s: NimBLE did not disconnect, error %d", peer.text().c_str(), result);
	}
}

void BleDiscovery::waitForDisconnect(uint32_t dtMs)
{
	closeWaitedMs += dtMs;

	if (closeWaitedMs < BleConnection::kCloseWaitMs)
	{
		return;
	}

	Logger::warn(kTag, "BLE probe of %s: the connection did not close within %u s", peer.text().c_str(), (unsigned)(BleConnection::kCloseWaitMs / 1000));
	finishDisconnect();
}

void BleDiscovery::finishDisconnect()
{
	scanner.releaseRadio(this);

	{
		LockGuard guard(&lock);
		connected = false;
	}

	services.clear();
	valuesToRead.clear();

	if (failed)
	{
		tableInProgress = GattTable();
		setState(State::Failed);
		return;
	}

	// before Done, which lets request() write peer again
	Logger::info(kTag, "BLE probe of %s: %u services, %u characteristics, %u values; disconnected", peer.text().c_str(),
		(unsigned)tableInProgress.services().size(), (unsigned)tableInProgress.characteristicCount(), (unsigned)tableInProgress.values().size());

	LockGuard guard(&lock);
	finishedTable = std::move(tableInProgress);
	tableInProgress = GattTable();
	state = State::Done;
}

void BleDiscovery::startRequest()
{
	LockGuard guard(&lock);
	gattRequest = GattRequest();
	gattRequest.state = RequestState::Running;
}

bool BleDiscovery::checkRequest(int& error)
{
	LockGuard guard(&lock);

	if (gattRequest.state == RequestState::Running || gattRequest.state == RequestState::None)
	{
		return false;
	}

	error = gattRequest.state == RequestState::Failed ? gattRequest.error : 0;
	gattRequest.state = RequestState::None;

	return true;
}

bool BleDiscovery::isConnected() const
{
	LockGuard guard(&lock);

	return connected;
}

void BleDiscovery::setState(State next)
{
	LockGuard guard(&lock);
	state = next;
}

}  // namespace w2b
