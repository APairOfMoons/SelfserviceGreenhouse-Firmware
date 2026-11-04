//
// 临界区适配层实现（C）：FreeRTOS 原生 taskENTER_CRITICAL / taskEXIT_CRITICAL。
//

#include "CriticalSection.h"

#include "FreeRTOS.h"
#include "task.h"

void greenhouseEnterCritical(void)
{
    taskENTER_CRITICAL();
}

void greenhouseExitCritical(void)
{
    taskEXIT_CRITICAL();
}