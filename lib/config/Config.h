#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Component.h"
#include "ConfigData.h"
#include "FixedString.h"

namespace w2b
{

class Lock;

// The running configuration every module holds; its lock guards each read and change. A change is edited under the lock on a staging
// copy; update(), on the application thread, then saves it, puts it in use and runs its follow-up. Writes /config.json through save.
class Config : public Component
{
public:
	// How the last change went
	enum class SaveState : uint8_t
	{
		// no change since boot
		Idle,

		// submitted; update() saves it
		Pending,

		// saved and in use
		Saved,

		// not saved; the configuration stays as it was
		Failed,
	};

	// What submit() did with a change
	enum class ChangeResult : uint8_t
	{
		// edited, valid, waiting for update()
		Accepted,

		// the edit refused it or the result is invalid; the error says why
		Invalid,

		// another change is pending
		Busy,
	};

	// Writes config to storage; false when that failed.
	using Save = std::function<bool(const ConfigData& config)>;

	// Edits newConfig, a copy of the running configuration, in place; false with a "field: reason" error refuses the change.
	using Edit = std::function<bool(ConfigData& newConfig, std::string& error)>;

	// runs on the application thread once the change is saved and in use
	using FollowUp = std::function<void()>;

	// lock: guards everything here; save: writes each change, on the application thread
	Config(Lock& lock, Save save);

	// "idle", "pending", "saved", "failed"
	static const char* nameOf(SaveState state);

	// nothing
	void setup() override;

	// Saves a pending change, puts it in use and runs its follow-up; the application thread's.
	void update(uint32_t dtMs) override;

	// Fills the running configuration in place, before the threads start: fill loads it or sets the defaults.
	void load(const std::function<void(ConfigData& config)>& fill);

	// a copy of the device section
	DeviceConfig device() const;

	// a copy of the wifi section
	WifiConfig wifi() const;

	// Copies the rule with the id into rule; false when none has it.
	bool findRule(const char* id, RuleConfig& rule) const;

	// Runs reader on the running configuration under the lock; reader must not call Config.
	void read(const std::function<void(const ConfigData& config)>& reader) const;

	// Runs edit now, under the lock, on a copy of the running configuration, and queues the result for update(). what: the change for
	// the log, "rule id0 added", read after edit ran; followUp may be empty.
	ChangeResult submit(const char* what, const Edit& edit, FollowUp followUp, std::string& error);

	// how the last change went
	SaveState saveState() const;

	// Goes up by one each time a configuration is applied, load() too; a module reads the configuration again when this changes.
	uint32_t currentRevision() const;

private:
	void finish(SaveState result);

	Lock& lock;

	Save save;

	ConfigData running;

	// the change in progress: submit() writes it, and nothing else touches it while Pending
	ConfigData staging;

	// a change's name for the log; a longer one is cut
	using ChangeName = FixedString<63>;

	ChangeName pendingWhat;

	FollowUp pendingFollowUp;

	SaveState state = SaveState::Idle;

	uint32_t revision = 0;
};

}  // namespace w2b
