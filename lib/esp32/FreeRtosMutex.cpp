#include "FreeRtosMutex.h"

namespace w2b
{

FreeRtosMutex::FreeRtosMutex()
	: handle(xSemaphoreCreateMutex())
{
}

FreeRtosMutex::~FreeRtosMutex()
{
	if (handle != nullptr)
	{
		vSemaphoreDelete(handle);
	}
}

void FreeRtosMutex::lock()
{
	if (handle != nullptr)
	{
		xSemaphoreTake(handle, portMAX_DELAY);
	}
}

void FreeRtosMutex::unlock()
{
	if (handle != nullptr)
	{
		xSemaphoreGive(handle);
	}
}

bool FreeRtosMutex::heldByCurrentTask() const
{
	return handle != nullptr && xSemaphoreGetMutexHolder(handle) == xTaskGetCurrentTaskHandle();
}

}  // namespace w2b
