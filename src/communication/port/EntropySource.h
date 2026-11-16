//
// Created by APairOfMoons on 2026/11/14.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ENTRYPSOURCE_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ENTRYPSOURCE_H

#include <cstdint>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * 上电随机数的熵源。
 *
 * 这一层只负责把原始值采出来；混合与格式化在 domain/BootSession，
 * 所以这部分不参与主机单测。
 */

/**
 * 采样 SysTick 计数器相位。
 *
 * 关键在于"两次读之间有没有被中断插入"——tick 与 UART 中断的落点每次上电都
 * 不同，这才是熵的来源，而不是计数器本身的值。
 */
uint32_t entropySystickPhase(void);

/** 当前栈位置的近似值。启动路径上的栈深度每次略有差别。 */
uint32_t entropyStackAddress(void);

#ifdef __cplusplus
}
#endif

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ENTRYPSOURCE_H
