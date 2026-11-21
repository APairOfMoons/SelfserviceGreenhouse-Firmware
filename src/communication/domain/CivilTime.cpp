//
// 纯整数的公历换算与 asctime 解析。
//

#include "CivilTime.h"

#include <cstddef>
#include <cstring>


namespace
{

constexpr int32_t kSecondsPerDay = 86400;
constexpr int32_t kDaysPer400Years = 146097;

const char *const kMonthNames[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/* asctime 的最短长度：24 个字符，不含结尾的 '\0' */
constexpr std::size_t kAsctimeLength = 24U;


bool isDigit(char value)
{
    return (value >= '0') && (value <= '9');
}


int twoDigits(const char *text)
{
    return ((text[0] - '0') * 10) + (text[1] - '0');
}


int fourDigits(const char *text)
{
    return ((text[0] - '0') * 1000) + ((text[1] - '0') * 100) +
           ((text[2] - '0') * 10) + (text[3] - '0');
}


bool isLeapYear(int year)
{
    return (((year % 4) == 0) && ((year % 100) != 0)) || ((year % 400) == 0);
}


unsigned daysInMonth(int year, unsigned month)
{
    static const uint8_t kLengths[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                         31U, 31U, 30U, 31U, 30U, 31U};

    if ((month == 0U) || (month > 12U))
    {
        return 0U;
    }

    if ((month == 2U) && isLeapYear(year))
    {
        return 29U;
    }

    return kLengths[month - 1U];
}


/** "Jan" → 1；认不出返回 0。 */
unsigned monthNumber(const char *text)
{
    for (unsigned index = 0U; index < 12U; ++index)
    {
        if (std::strncmp(text, kMonthNames[index], 3U) == 0)
        {
            return index + 1U;
        }
    }

    return 0U;
}

} // namespace


bool CivilTime::parseAsctime(const char *text, uint32_t *epochSeconds)
{
    if ((text == nullptr) || (epochSeconds == nullptr))
    {
        return false;
    }

    /* 先确认长度，后面的定点访问才不会越界读到字符串结尾之外 */
    if (std::strlen(text) < kAsctimeLength)
    {
        return false;
    }

    /* 分隔符固定：星期/月/日之间是空格，时分秒之间是冒号 */
    if ((text[3] != ' ') || (text[7] != ' ') || (text[10] != ' ') ||
        (text[13] != ':') || (text[16] != ':'))
    {
        return false;
    }

    const unsigned month = monthNumber(&text[4]);
    if (month == 0U)
    {
        return false;
    }

    /* 日：asctime 左补空格，所以第 8 位可能是空格 */
    if (!(((text[8] == ' ') || isDigit(text[8])) && isDigit(text[9])))
    {
        return false;
    }

    if (!(isDigit(text[11]) && isDigit(text[12]) && isDigit(text[14]) &&
          isDigit(text[15]) && isDigit(text[17]) && isDigit(text[18]) &&
          isDigit(text[20]) && isDigit(text[21]) && isDigit(text[22]) &&
          isDigit(text[23])))
    {
        return false;
    }

    const int year = fourDigits(&text[20]);
    const int day = (text[8] == ' ') ? (text[9] - '0') : twoDigits(&text[8]);
    const int hour = twoDigits(&text[11]);
    const int minute = twoDigits(&text[14]);
    const int second = twoDigits(&text[17]);

    if ((day < 1) || (static_cast<unsigned>(day) > daysInMonth(year, month)))
    {
        return false;
    }

    if ((hour > 23) || (minute > 59) || (second > 59))
    {
        return false;
    }

    const int32_t days = daysFromCivil(year, month, static_cast<unsigned>(day));
    if (days < 0)
    {
        /* uint32 只覆盖 1970 之后；SNTP 没同步上时给的是 1970，正好是 0 天 */
        return false;
    }

    *epochSeconds = (static_cast<uint32_t>(days) * 86400U) +
                    (static_cast<uint32_t>(hour) * 3600U) +
                    (static_cast<uint32_t>(minute) * 60U) +
                    static_cast<uint32_t>(second);
    return true;
}


uint32_t CivilTime::timeOfDayMs(uint32_t epochSeconds, int32_t offsetMinutes)
{
    /*
     * 先各取模再相加，全程 32 位：epoch 本身装不进 int32，但"当天秒数"装得进。
     * 负数偏移靠补一个 86400 归一，结果与 64 位算法一致。
     */
    int32_t secondOfDay = static_cast<int32_t>(epochSeconds % 86400U) + (offsetMinutes * 60);
    secondOfDay %= kSecondsPerDay;
    if (secondOfDay < 0)
    {
        secondOfDay += kSecondsPerDay;
    }

    return static_cast<uint32_t>(secondOfDay) * 1000U;
}


int32_t CivilTime::daysFromCivil(int year, unsigned month, unsigned day)
{
    /*
     * Howard Hinnant 的 days_from_civil：把 3 月当作一年的开头，闰日就落到了
     * 年末，2 月不用单独处理。全部整数运算，没有查表也没有循环。
     */
    const int adjustedYear = (month <= 2U) ? (year - 1) : year;
    const int era = ((adjustedYear >= 0) ? adjustedYear : (adjustedYear - 399)) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - (era * 400));
    const unsigned dayOfYear =
        (((153U * ((month > 2U) ? (month - 3U) : (month + 9U))) + 2U) / 5U) + day - 1U;
    const unsigned dayOfEra = (yearOfEra * 365U) + (yearOfEra / 4U) -
                              (yearOfEra / 100U) + dayOfYear;

    return static_cast<int32_t>((era * kDaysPer400Years) +
                                static_cast<int32_t>(dayOfEra) - 719468);
}
