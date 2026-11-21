//
// 软件实时时钟（Software RTC）：由通信线程写入 SNTP 校时基准，控制线程读取当前时刻。
//
// 并发：采用 CriticalSection（FreeRTOS 临界区；LOGIC_HOST_TEST 下为空操作）保护共享基准状态。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_RTCCLOCK_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_RTCCLOCK_H

#include <cstddef>
#include <cstdint>

struct RtcDateTime
{
    uint16_t year;   // >= 2024
    uint8_t month;   // 1..12
    uint8_t day;     // 1..31
    uint8_t hour;    // 0..23
    uint8_t minute;  // 0..59
    uint8_t second;  // 0..59
    uint8_t weekday; // 0=Sun..6=Sat
};

struct RtcLogTime
{
    char text[24];
};

class RtcClock
{
    public:
    static constexpr std::size_t kLogTimeBufferSize = sizeof(RtcLogTime::text);

    static bool updateFromSntp(const RtcDateTime &dateTime, uint32_t syncUptimeMs);
    static bool isSynced();
    static uint32_t version();
    static bool readTimeOfDayMs(uint32_t currentUptimeMs, uint32_t *outMsOfDay);
    static bool snapshot(uint32_t currentUptimeMs, RtcDateTime *outDateTime);
    static bool formatLogTime(uint32_t currentUptimeMs, char *buffer, std::size_t capacity);
    static RtcLogTime logTime(uint32_t currentUptimeMs);
    static void reset();
};

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_RTCCLOCK_H
