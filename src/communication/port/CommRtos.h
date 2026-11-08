//
// Created by APairOfMoons on 2026/11/7.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_COMMRTOS_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_COMMRTOS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * FreeRTOS 的 C 适配层。
 *
 * 通信模块是 C++，但 C++ 翻译单元里不能直接包含 FreeRTOS.h：FSP 的 bsp_tfu.h
 * 在 extern "C" 中引入了 <cmath>，会让随后的 C++ 标准库头文件报错
 * （设计文档约束 C10）。所以所有 RTOS 调用集中在这个 C 文件里，
 * C++ 侧只看到下面这道窄接口。
 *
 * 线程约定：所有函数只能在任务上下文调用，且必须在调度器启动之后。
 * 通信线程的 initialize() 由 comm_thread 自身在 run() 开头调用，满足这一前提。
 */

/** 队列句柄。调用方把它当不透明指针保存，不关心内部结构。 */
typedef void * CommRtosQueue;

/**
 * 静态队列可用的存储上限（字节）。
 *
 * 通信模块只有一个上行队列：8 条消息，每条 292 字节，共 2336 字节。
 * 这里留了约 700 字节余量，改动消息结构时不至于立刻撞上限。
 */
#define COMM_RTOS_QUEUE_STORAGE_BYTES (3072U)

/**
 * 静态创建一个队列。
 *
 * 用静态存储而不是 xQueueCreate()，好处是堆水位在运行期完全不变，
 * 长稳测试里 xPortGetFreeHeapSize() 的任何下降都只可能来自别处。
 * 代价是这个适配层只支持一个队列——通信模块正好只需要一个。
 *
 * @param length   队列深度（条）
 * @param itemSize 单条消息字节数
 * @return 队列句柄；参数非法、容量超出静态存储、或已经创建过一个队列时返回 NULL
 */
CommRtosQueue commRtosQueueCreate(uint32_t length, uint32_t itemSize);

/**
 * 入队。
 *
 * timeoutMs 为 0 时立即返回，不阻塞调用者——上行路径就是用这种方式
 * 保证逻辑线程不会被通信阻塞。
 */
bool commRtosQueueSend(CommRtosQueue queue, const void * item, uint32_t timeoutMs);

/**
 * 出队。
 *
 * timeoutMs 为 0 时队列为空立即返回 false；非 0 时最多等待这么久。
 */
bool commRtosQueueReceive(CommRtosQueue queue, void * item, uint32_t timeoutMs);

/** 当前排队条数。 */
uint32_t commRtosQueueCount(CommRtosQueue queue);

/** 清空队列，丢弃全部积压（断线重连后用它扔掉陈旧数据）。 */
void commRtosQueueReset(CommRtosQueue queue);

/** 进入临界区。可嵌套，必须与 commRtosExitCritical() 成对出现。 */
void commRtosEnterCritical(void);

/** 退出临界区。 */
void commRtosExitCritical(void);

/** 阻塞延时，单位毫秒。 */
void commRtosDelay(uint32_t milliseconds);

/** 上电以来的秒数。tick 为 32 位、1000 Hz，约 49 天回绕。 */
uint32_t commRtosUptimeSeconds(void);

/**
 * 上电以来的毫秒数。
 *
 * 就是 tick 计数本身（1000 Hz），同样约 49 天回绕。比较时间一律写成
 * `(now - start) >= timeout` 这种无符号差值，回绕时仍然正确。
 */
uint32_t commRtosUptimeMs(void);

/** 当前剩余堆字节数。只用于长稳观察，不参与任何逻辑判断。 */
uint32_t commRtosFreeHeapBytes(void);

#ifdef __cplusplus
}
#endif

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_COMMRTOS_H
