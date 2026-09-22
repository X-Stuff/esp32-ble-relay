#include "Config.h"

#include <utility>

#include "Lock.h"
#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "config";

}  // namespace

Config::Config(Lock& lock, Save save)
	: lock(lock), save(std::move(save))
{
}

const char* Config::nameOf(SaveState state)
{
	switch (state)
	{
		case SaveState::Idle:    return "idle";
		case SaveState::Pending: return "pending";
		case SaveState::Saved:   return "saved";
		case SaveState::Failed:  return "failed";
	}

	return "?";
}

void Config::setup()
{
}

// Nothing writes staging, pendingWhat or pendingFollowUp while Pending, so they are read here without the lock.
void Config::update(uint32_t /*dtMs*/)
{
	{
		LockGuard guard(&lock);

		if (state != SaveState::Pending)
		{
			return;
		}
	}

	if (!save || !save(staging))
	{
		Logger::warn(kTag, "%s: not saved; the configuration stays as it was", pendingWhat.c_str());
		finish(SaveState::Failed);
		return;
	}

	{
		LockGuard guard(&lock);
		running = staging;
		++revision;
	}

	Logger::info(kTag, "%s: saved", pendingWhat.c_str());

	if (pendingFollowUp)
	{
		pendingFollowUp();
	}

	finish(SaveState::Saved);
}

void Config::load(const std::function<void(ConfigData& config)>& fill)
{
	LockGuard guard(&lock);
	fill(running);
	++revision;
}

DeviceConfig Config::device() const
{
	LockGuard guard(&lock);
	return running.device();
}

WifiConfig Config::wifi() const
{
	LockGuard guard(&lock);
	return running.wifi();
}

bool Config::findRule(const char* id, RuleConfig& rule) const
{
	LockGuard guard(&lock);
	const RuleConfig* found = running.findRule(id);

	if (found == nullptr)
	{
		return false;
	}

	rule = *found;

	return true;
}

void Config::read(const std::function<void(const ConfigData& config)>& reader) const
{
	LockGuard guard(&lock);
	reader(running);
}

// Busy comes first: while a change is pending, staging holds it.
Config::ChangeResult Config::submit(const char* what, const Edit& edit, FollowUp followUp, std::string& error)
{
	LockGuard guard(&lock);

	if (state == SaveState::Pending)
	{
		error = "another configuration change is being saved";
		return ChangeResult::Busy;
	}

	if (!edit)
	{
		error = "no change given";
		return ChangeResult::Invalid;
	}

	staging = running;

	if (!edit(staging, error) || !staging.validate(error))
	{
		return ChangeResult::Invalid;
	}

	pendingWhat.assign(what);
	pendingFollowUp = std::move(followUp);
	state = SaveState::Pending;

	return ChangeResult::Accepted;
}

Config::SaveState Config::saveState() const
{
	LockGuard guard(&lock);
	return state;
}

uint32_t Config::currentRevision() const
{
	LockGuard guard(&lock);

	return revision;
}

void Config::finish(SaveState result)
{
	LockGuard guard(&lock);
	pendingFollowUp = nullptr;
	state = result;
}

}  // namespace w2b
