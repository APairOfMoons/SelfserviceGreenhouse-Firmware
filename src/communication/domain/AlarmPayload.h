//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ALARMPAYLOAD_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ALARMPAYLOAD_H

#include <cstddef>
#include <cstdint>

#include "communication/GreenhouseException.h"


/**
 * 告警报文。
 *
 * 每类告警占一个 topic（gh/<deviceId>/alarm/<code>），各自 retain，
 * 这样客户端重连后能直接看到"当前有哪些告警"，不同告警也不会互相覆盖。
 */
class AlarmPayload
{
    public:
    /* 枚举到 topic 片段／JSON 字段的映射；未知枚举返回 "unknown" */
    static const char *code(GreenhouseException exception);

    /**
     * @param buffer   输出缓冲区，成功时以 '\0' 结尾
     * @param capacity 缓冲区容量（含结尾 '\0'）
     * @return 写入长度；容量不足时返回 0
     */
    static std::size_t build(GreenhouseException exception,
                             uint32_t uptimeSeconds,
                             char *buffer,
                             std::size_t capacity);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ALARMPAYLOAD_H
