//
// Created by Codex on 2026/11/21.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_SNTPTIMEPARSER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_SNTPTIMEPARSER_H

#include <cstdint>

#include "configuration/RtcClock.h"


/**
 * ESP-AT SNTP 时间响应解析器。
 *
 * 解析 `AT+CIPSNTPTIME?` 返回的 `+CIPSNTPTIME:<asctime>` 行或裸 `<asctime>` 字符串，
 * 例如 `+CIPSNTPTIME:Mon Dec 12 02:33:32 2016`、`Thu Jan  1 08:00:00 1970`。
 * 保持纯 C++11 无堆分配与无平台依赖，便于主机端直接单测。
 */
class SntpTimeParser
{
    public:
    /**
     * 解析完整 `+CIPSNTPTIME:<asctime>` 行或裸 `<asctime>` 字符串。
     *
     * @param line        待解析的 C 字符串（可含冒号后可选空白、单/双位日期及行尾空白）
     * @param outDateTime 解析成功时写入日历时间；失败时保持原值不变
     * @return 格式、星期、月份、闰年/当月天数、时分秒均合法且 year >= 2024 时返回 true
     */
    static bool parseLine(const char *line, RtcDateTime *outDateTime);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_SNTPTIMEPARSER_H
