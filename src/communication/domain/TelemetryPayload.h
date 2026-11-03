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
 * 字段全部为整数：温度与湿度以 0.1 为单位（235 表示 23.5），
 * 避免依赖 printf 浮点支持，也避免客户端出现精度歧义。
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
