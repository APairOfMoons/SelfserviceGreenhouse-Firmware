//
// Created by APairOfMoons on 2026/11/7.
//

/*
 * FreeRTOS 的 C 适配层实现。
 *
 * 这是整个通信模块里唯一包含 FreeRTOS.h 的翻译单元，理由见 CommRtos.h 与
 * 设计文档约束 C10。保持它足够薄：只做类型转换与参数校验，不放业务逻辑。
 */

#include "CommRtos.h"

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"
#include "task.h"

/* ---- 单队列的静态存储 ---- */

static StaticQueue_t s_queueControl;

/*
 * FreeRTOS 要求队列存储区按 portBYTE_ALIGNMENT 对齐。这里用 uint32_t 数组
 * 保证至少 4 字节对齐，同时把 3072 断言成 4 的倍数（见下）。
 */
static uint32_t s_queueStorage[COMM_RTOS_QUEUE_STORAGE_BYTES / 4U];

/* 本适配层只支持一个队列；重复创建说明调用方写错了，直接返回 NULL。 */
static bool s_queueInUse = false;

/* 存储区大小必须是 4 的倍数，否则上面的数组会被截短。 */
typedef char CommRtosQueueStorageSizeIsMultipleOfFour[(COMM_RTOS_QUEUE_STORAGE_BYTES % 4U) == 0U ? 1 : -1];

/* ---- 队列 ---- */

CommRtosQueue commRtosQueueCreate(uint32_t length, uint32_t itemSize)
{
    if (s_queueInUse || (length == 0U) || (itemSize == 0U))
    {
        return NULL;
    }

    /* 先做除法再乘法，避免 length * itemSize 溢出 */
    if (length > (COMM_RTOS_QUEUE_STORAGE_BYTES / itemSize))
    {
        return NULL;
    }

    QueueHandle_t handle = xQueueCreateStatic((UBaseType_t) length,
                                              (UBaseType_t) itemSize,
                                              (uint8_t *) s_queueStorage,
                                              &s_queueControl);
    if (handle == NULL)
    {
        return NULL;
    }

    s_queueInUse = true;
    return (CommRtosQueue) handle;
}

bool commRtosQueueSend(CommRtosQueue queue, const void * item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    return xQueueSend((QueueHandle_t) queue, item, pdMS_TO_TICKS(timeoutMs)) == pdPASS;
}

bool commRtosQueueReceive(CommRtosQueue queue, void * item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    return xQueueReceive((QueueHandle_t) queue, item, pdMS_TO_TICKS(timeoutMs)) == pdPASS;
}

uint32_t commRtosQueueCount(CommRtosQueue queue)
{
    if (queue == NULL)
    {
        return 0U;
    }

    return (uint32_t) uxQueueMessagesWaiting((QueueHandle_t) queue);
}

void commRtosQueueReset(CommRtosQueue queue)
{
    if (queue == NULL)
    {
        return;
    }

    xQueueReset((QueueHandle_t) queue);
}

/* ---- 临界区 ---- */

/*
 * 直接借用 FreeRTOS 的实现：它按 BASEPRI 屏蔽、带嵌套计数，
 * 且与调度器的临界区可以互相嵌套。注意它只在调度器启动之后有效，
 * 这一点写在头文件的线程约定里。
 */
void commRtosEnterCritical(void)
{
    taskENTER_CRITICAL();
}

void commRtosExitCritical(void)
{
    taskEXIT_CRITICAL();
}

/* ---- 时间 ---- */

void commRtosDelay(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

uint32_t commRtosUptimeSeconds(void)
{
    return (uint32_t) (xTaskGetTickCount() / (TickType_t) configTICK_RATE_HZ);
}

uint32_t commRtosUptimeMs(void)
{
    return (uint32_t) xTaskGetTickCount();
}

uint32_t commRtosFreeHeapBytes(void)
{
    return (uint32_t) xPortGetFreeHeapSize();
}
