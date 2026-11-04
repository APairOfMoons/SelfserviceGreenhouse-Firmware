//
// 临界区适配层：对上层提供 C 接口，内部用 FreeRTOS 原生宏。
// 之所以单独用 .c 实现，是因为在 C++ 里直接包含 FreeRTOS.h 会与 FSP 的
// bsp_tfu.h（在 extern "C" 中引入 <cmath>）冲突。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_CRITICALSECTION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_CRITICALSECTION_H

#ifdef __cplusplus
extern "C" {
#endif

void greenhouseEnterCritical(void);
void greenhouseExitCritical(void);

#ifdef __cplusplus
}
#endif

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_CRITICALSECTION_H