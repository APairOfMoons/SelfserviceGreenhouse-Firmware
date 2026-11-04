//
// Created by APairOfMoons on 2026/11/4.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATIONPAYLOAD_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATIONPAYLOAD_H

#include <cstddef>
#include <cstdint>

#include "configuration/LogicalConfiguration.h"


/**
 * 配置下发的报文编解码。
 *
 * 客户端只发"要改的字段"，未出现的字段保持基线值。因此调用方先取一份
 * LogicalConfiguration 快照作为基线，解码后再交给 modifyConfiguration()，
 * 由它做物理护栏与自洽性校验（语法正确不等于语义合法）。
 *
 * 数值约定与遥测一致：温度/湿度用 0.01 单位整数（2350 = 23.50），
 * 光照用 lx，时间一律毫秒。字段名见 decode() 的实现。
 */
class ConfigurationPayload
{
    public:
    /**
     * 把 JSON 报文覆盖到基线上。未知键被忽略（前向兼容），语法错误返回 false。
     *
     * @param payload 原始字节，不要求以 '\0' 结尾
     * @param length  字节数
     * @param inOut   输入基线 / 输出候选配置；失败时内容可能已被部分修改，调用方应丢弃
     * @return 语法合法且所有出现的字段都能转换时返回 true
     */
    static bool decode(const char *payload, std::size_t length, VariableStatics *inOut);

    /**
     * 编解码结果回执。
     *
     * @param accepted 是否被 modifyConfiguration() 接受
     * @param reason   失败原因，成功时可传 nullptr
     * @return 写入长度；容量不足返回 0
     */
    static std::size_t buildAck(bool accepted,
                                const char *reason,
                                char *buffer,
                                std::size_t capacity);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATIONPAYLOAD_H
