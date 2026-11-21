//
// 软件实时时钟实现：纯 C++11/14，无堆分配、无异常、无 RTTI。
//

#include "configuration/RtcClock.h"

#include "configuration/CriticalSection.h"

#if defined(LOGIC_HOST_TEST)
#define GREENHOUSE_ENTER_CRITICAL() ((void) 0)
#define GREENHOUSE_EXIT_CRITICAL()  ((void) 0)
#else
#define GREENHOUSE_ENTER_CRITICAL() greenhouseEnterCritical()
#define GREENHOUSE_EXIT_CRITICAL()  greenhouseExitCritical()
#endif

namespace
{

constexpr uint32_t kSecondMs = 1000UL;
constexpr uint32_t kMinuteMs = 60UL * kSecondMs;
constexpr uint32_t kHourMs = 60UL * kMinuteMs;
constexpr uint32_t kDayMs = 24UL * kHourMs;
constexpr uint16_t kMinValidYear = 2024U;

struct RtcState
{
    bool synced;
    uint32_t version;
    RtcDateTime baseDateTime;
    uint32_t baseMsOfDay;
    uint32_t syncUptimeMs;
};

RtcState g_rtcState = {false, 0UL, {0U, 0U, 0U, 0U, 0U, 0U, 0U}, 0UL, 0UL};

bool isLeapYear(uint16_t year)
{
    return (((year % 4U) == 0U) && ((year % 100U) != 0U)) || ((year % 400U) == 0U);
}

uint8_t daysInMonth(uint16_t year, uint8_t month)
{
    static constexpr uint8_t kDaysPerMonth[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
    {
        return 0U;
    }

    if ((month == 2U) && isLeapYear(year))
    {
        return 29U;
    }

    return kDaysPerMonth[month - 1U];
}

bool isValidDateTime(const RtcDateTime &dt)
{
    if (dt.year < kMinValidYear)
    {
        return false;
    }

    const uint8_t maxDay = daysInMonth(dt.year, dt.month);
    if ((0U == maxDay) || (dt.day < 1U) || (dt.day > maxDay))
    {
        return false;
    }

    if ((dt.hour > 23U) || (dt.minute > 59U) || (dt.second > 59U) || (dt.weekday > 6U))
    {
        return false;
    }

    return true;
}

void advanceDays(RtcDateTime &dt, uint32_t extraDays)
{
    if (0UL == extraDays)
    {
        return;
    }

    dt.weekday = static_cast<uint8_t>((static_cast<uint32_t>(dt.weekday) + extraDays) % 7UL);

    uint32_t remainingDays = extraDays;
    while (remainingDays > 0UL)
    {
        const uint8_t dim = daysInMonth(dt.year, dt.month);
        const uint32_t daysLeftInMonth = static_cast<uint32_t>(dim - dt.day);
        if (remainingDays <= daysLeftInMonth)
        {
            dt.day = static_cast<uint8_t>(dt.day + remainingDays);
            remainingDays = 0UL;
        }
        else
        {
            remainingDays -= (daysLeftInMonth + 1UL);
            dt.day = 1U;
            if (dt.month < 12U)
            {
                dt.month++;
            }
            else
            {
                dt.month = 1U;
                dt.year++;
            }
        }
    }
}

void copyState(RtcState *out)
{
    GREENHOUSE_ENTER_CRITICAL();
    *out = g_rtcState;
    GREENHOUSE_EXIT_CRITICAL();
}

bool computeSnapshot(uint32_t currentUptimeMs, RtcDateTime *outDateTime, uint16_t *outMillisecond)
{
    RtcState state = {};
    copyState(&state);
    if (!state.synced)
    {
        return false;
    }

    const uint32_t elapsedMs = currentUptimeMs - state.syncUptimeMs;
    const uint32_t elapsedDays = elapsedMs / kDayMs;
    const uint32_t elapsedRemMs = elapsedMs % kDayMs;
    const uint32_t sumMs = state.baseMsOfDay + elapsedRemMs;
    const uint32_t extraDays = elapsedDays + (sumMs / kDayMs);
    const uint32_t msOfDay = sumMs % kDayMs;

    RtcDateTime result = state.baseDateTime;
    advanceDays(result, extraDays);

    const uint32_t totalSeconds = msOfDay / kSecondMs;
    result.hour = static_cast<uint8_t>(totalSeconds / 3600UL);
    result.minute = static_cast<uint8_t>((totalSeconds % 3600UL) / 60UL);
    result.second = static_cast<uint8_t>(totalSeconds % 60UL);

    *outDateTime = result;
    if (nullptr != outMillisecond)
    {
        *outMillisecond = static_cast<uint16_t>(msOfDay % kSecondMs);
    }
    return true;
}

void writeDigits(char *dst, uint32_t value, uint8_t width)
{
    for (uint8_t i = width; i > 0U; --i)
    {
        dst[i - 1U] = static_cast<char>('0' + (value % 10UL));
        value /= 10UL;
    }
}

std::size_t writeUnsigned(char *dst, uint32_t value)
{
    char digits[10];
    std::size_t count = 0U;

    do
    {
        digits[count++] = static_cast<char>('0' + (value % 10UL));
        value /= 10UL;
    } while (value > 0UL);

    for (std::size_t i = 0U; i < count; ++i)
    {
        dst[i] = digits[count - 1U - i];
    }

    return count;
}

} // namespace

bool RtcClock::updateFromSntp(const RtcDateTime &dateTime, uint32_t syncUptimeMs)
{
    if (!isValidDateTime(dateTime))
    {
        return false;
    }

    const uint32_t baseMsOfDay =
        ((static_cast<uint32_t>(dateTime.hour) * 3600UL) +
         (static_cast<uint32_t>(dateTime.minute) * 60UL) +
         static_cast<uint32_t>(dateTime.second)) *
        kSecondMs;

    GREENHOUSE_ENTER_CRITICAL();
    g_rtcState.synced = true;
    g_rtcState.version++;
    if (0UL == g_rtcState.version)
    {
        g_rtcState.version = 1UL;
    }
    g_rtcState.baseDateTime = dateTime;
    g_rtcState.baseMsOfDay = baseMsOfDay;
    g_rtcState.syncUptimeMs = syncUptimeMs;
    GREENHOUSE_EXIT_CRITICAL();

    return true;
}

bool RtcClock::isSynced()
{
    bool synced = false;
    GREENHOUSE_ENTER_CRITICAL();
    synced = g_rtcState.synced;
    GREENHOUSE_EXIT_CRITICAL();
    return synced;
}

uint32_t RtcClock::version()
{
    uint32_t value = 0UL;
    GREENHOUSE_ENTER_CRITICAL();
    value = g_rtcState.version;
    GREENHOUSE_EXIT_CRITICAL();
    return value;
}

bool RtcClock::readTimeOfDayMs(uint32_t currentUptimeMs, uint32_t *outMsOfDay)
{
    if (nullptr == outMsOfDay)
    {
        return false;
    }

    RtcState state = {};
    copyState(&state);
    if (!state.synced)
    {
        return false;
    }

    const uint32_t elapsedMs = currentUptimeMs - state.syncUptimeMs;
    *outMsOfDay = (state.baseMsOfDay + (elapsedMs % kDayMs)) % kDayMs;
    return true;
}

bool RtcClock::snapshot(uint32_t currentUptimeMs, RtcDateTime *outDateTime)
{
    if (nullptr == outDateTime)
    {
        return false;
    }

    return computeSnapshot(currentUptimeMs, outDateTime, nullptr);
}

bool RtcClock::formatLogTime(uint32_t currentUptimeMs, char *buffer, std::size_t capacity)
{
    if ((nullptr == buffer) || (capacity < kLogTimeBufferSize))
    {
        if ((nullptr != buffer) && (capacity > 0U))
        {
            buffer[0] = '\0';
        }
        return false;
    }

    RtcDateTime dt = {};
    uint16_t ms = 0U;
    if (computeSnapshot(currentUptimeMs, &dt, &ms))
    {
        writeDigits(buffer + 0U, dt.year, 4U);
        buffer[4] = '-';
        writeDigits(buffer + 5U, dt.month, 2U);
        buffer[7] = '-';
        writeDigits(buffer + 8U, dt.day, 2U);
        buffer[10] = ' ';
        writeDigits(buffer + 11U, dt.hour, 2U);
        buffer[13] = ':';
        writeDigits(buffer + 14U, dt.minute, 2U);
        buffer[16] = ':';
        writeDigits(buffer + 17U, dt.second, 2U);
        buffer[19] = '.';
        writeDigits(buffer + 20U, ms, 3U);
        buffer[23] = '\0';
        return true;
    }

    const std::size_t len = writeUnsigned(buffer, currentUptimeMs);
    buffer[len] = ' ';
    buffer[len + 1U] = 'm';
    buffer[len + 2U] = 's';
    buffer[len + 3U] = '\0';
    return true;
}

RtcLogTime RtcClock::logTime(uint32_t currentUptimeMs)
{
    RtcLogTime result = {};
    (void) formatLogTime(currentUptimeMs, result.text, sizeof(result.text));
    return result;
}

void RtcClock::reset()
{
    GREENHOUSE_ENTER_CRITICAL();
    g_rtcState.synced = false;
    g_rtcState.version = 0UL;
    g_rtcState.baseDateTime = {0U, 0U, 0U, 0U, 0U, 0U, 0U};
    g_rtcState.baseMsOfDay = 0UL;
    g_rtcState.syncUptimeMs = 0UL;
    GREENHOUSE_EXIT_CRITICAL();
}
