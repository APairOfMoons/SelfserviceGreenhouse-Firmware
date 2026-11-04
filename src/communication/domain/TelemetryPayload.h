//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYPAYLOAD_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYPAYLOAD_H

#include <cstddef>
#include <cstdint>

#include "communication/GreenhouseData.h"


/**
 * 把 MonitoringData 序列化为遥测 JSON。
 *
 * 温度与湿度以 0.01 为单位（2350 表示 23.50），用 int32 承载，
 * 精度比常见传感器（0.0625 ℃）还细一档，同时不依赖 printf 浮点支持。
 * 光照本身就是整数 lux，原样上报。
 */
class TelemetryPayload
{
    public:
    /**
     * @param buffer   输出缓冲区，成功时以 '\0' 结尾
     * @param capacity 缓冲区容量（含结尾 '\0'）
     * @return 写入长度；容量不足时返回 0，且 buffer 内容不可用
     */
    static std::size_t build(const MonitoringData &data,
                             uint32_t sequence,
                             uint32_t uptimeSeconds,
                             char *buffer,
                             std::size_t capacity);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYPAYLOAD_H
