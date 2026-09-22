#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Lock.h"

namespace w2b
{

// Lock over a non-recursive FreeRTOS mutex. Safe as a global: FreeRTOS allows creating a mutex before the scheduler starts.
// Firmware only.
class FreeRtosMutex : public Lock
{
public:
	// creates the mutex; when FreeRTOS cannot, every call does nothing
	FreeRtosMutex();

	~FreeRtosMutex() override;

	FreeRtosMutex(const FreeRtosMutex&) = delete;

	FreeRtosMutex& operator=(const FreeRtosMutex&) = delete;

	// waits for the mutex without a timeout
	void lock() override;

	// releases the mutex held by the calling task
	void unlock() override;

	// true when the calling task holds the mutex
	bool heldByCurrentTask() const;

private:
	// null when FreeRTOS could not create the mutex
	SemaphoreHandle_t handle;
};

}  // namespace w2b
