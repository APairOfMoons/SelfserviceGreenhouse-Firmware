//
// Created by Codex on 2026/11/21.
//

#include "SntpTimeParser.h"

#include <cstddef>
#include <cstring>


namespace
{

constexpr char kPrefix[] = "+CIPSNTPTIME:";
constexpr std::size_t kPrefixLength = sizeof(kPrefix) - 1U;
constexpr uint16_t kMinValidYear = 2024U;

const char *const kWeekdays[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const char *const kMonths[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const uint8_t kDaysPerMonth[12] = {
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};


bool isAsciiDigit(char ch)
{
    return (ch >= '0') && (ch <= '9');
}


bool isHorizontalSpace(char ch)
{
    return (ch == ' ') || (ch == '\t');
}


bool skipSpaces(const char **cursor)
{
    if (**cursor != ' ')
    {
        return false;
    }

    while (**cursor == ' ')
    {
        ++(*cursor);
    }

    return true;
}


bool parseTokenIndex(const char **cursor,
                     const char *const *table,
                     std::size_t count,
                     uint8_t *outIndex)
{
    for (std::size_t i = 0U; i < count; ++i)
    {
        if ((std::strncmp(*cursor, table[i], 3U) == 0) && ((*cursor)[3] == ' '))
        {
            *outIndex = static_cast<uint8_t>(i);
            *cursor += 3U;
            return true;
        }
    }

    return false;
}


bool parseDay(const char **cursor, uint8_t *outDay)
{
    const char *p = *cursor;
    if (!isAsciiDigit(p[0]))
    {
        return false;
    }

    uint8_t value = static_cast<uint8_t>(p[0] - '0');
    ++p;

    if (isAsciiDigit(p[0]))
    {
        value = static_cast<uint8_t>((value * 10U) + static_cast<uint8_t>(p[0] - '0'));
        ++p;
    }

    *outDay = value;
    *cursor = p;
    return true;
}


bool parseTwoDigits(const char **cursor, uint8_t *outValue)
{
    const char *p = *cursor;
    if (!isAsciiDigit(p[0]) || !isAsciiDigit(p[1]))
    {
        return false;
    }

    *outValue = static_cast<uint8_t>(((p[0] - '0') * 10) + (p[1] - '0'));
    *cursor = p + 2U;
    return true;
}


bool parseFourDigits(const char **cursor, uint16_t *outValue)
{
    const char *p = *cursor;
    uint16_t value = 0U;

    for (std::size_t i = 0U; i < 4U; ++i)
    {
        if (!isAsciiDigit(p[i]))
        {
            return false;
        }

        value = static_cast<uint16_t>((value * 10U) + static_cast<uint16_t>(p[i] - '0'));
    }

    *outValue = value;
    *cursor = p + 4U;
    return true;
}


bool isLeapYear(uint16_t year)
{
    return ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}


uint8_t daysInMonth(uint16_t year, uint8_t month)
{
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

} // namespace


bool SntpTimeParser::parseLine(const char *line, RtcDateTime *outDateTime)
{
    if ((line == nullptr) || (outDateTime == nullptr))
    {
        return false;
    }

    const char *cursor = line;
    while (isHorizontalSpace(*cursor))
    {
        ++cursor;
    }

    if (cursor[0] == '+')
    {
        if (std::strncmp(cursor, kPrefix, kPrefixLength) != 0)
        {
            return false;
        }

        cursor += kPrefixLength;
        while (isHorizontalSpace(*cursor))
        {
            ++cursor;
        }
    }

    uint8_t weekday = 0U;
    if (!parseTokenIndex(&cursor, kWeekdays, 7U, &weekday) || !skipSpaces(&cursor))
    {
        return false;
    }

    uint8_t monthIndex = 0U;
    if (!parseTokenIndex(&cursor, kMonths, 12U, &monthIndex) || !skipSpaces(&cursor))
    {
        return false;
    }

    uint8_t day = 0U;
    if (!parseDay(&cursor, &day) || !skipSpaces(&cursor))
    {
        return false;
    }

    uint8_t hour = 0U;
    uint8_t minute = 0U;
    uint8_t second = 0U;
    if (!parseTwoDigits(&cursor, &hour) || (*cursor != ':'))
    {
        return false;
    }
    ++cursor;

    if (!parseTwoDigits(&cursor, &minute) || (*cursor != ':'))
    {
        return false;
    }
    ++cursor;

    if (!parseTwoDigits(&cursor, &second) || !skipSpaces(&cursor))
    {
        return false;
    }

    uint16_t year = 0U;
    if (!parseFourDigits(&cursor, &year))
    {
        return false;
    }

    while (isHorizontalSpace(*cursor) || (*cursor == '\r') || (*cursor == '\n'))
    {
        ++cursor;
    }

    if (*cursor != '\0')
    {
        return false;
    }

    const uint8_t month = static_cast<uint8_t>(monthIndex + 1U);
    if ((year < kMinValidYear) || (hour > 23U) || (minute > 59U) || (second > 59U))
    {
        return false;
    }

    const uint8_t maxDay = daysInMonth(year, month);
    if ((day < 1U) || (day > maxDay))
    {
        return false;
    }

    RtcDateTime parsed = {};
    parsed.year = year;
    parsed.month = month;
    parsed.day = day;
    parsed.hour = hour;
    parsed.minute = minute;
    parsed.second = second;
    parsed.weekday = weekday;
    *outDateTime = parsed;
    return true;
}
