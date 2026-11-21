//
// SNTP 解析与软件 RTC 主机端单元测试（不进固件）。
//
// 用法（在仓库根目录，主机编译器，C++11）：
//   g++ -std=c++11 -DLOGIC_HOST_TEST -Isrc
//       src/communication/domain/SntpTimeParser.cpp
//       src/configuration/RtcClock.cpp
//       src/configuration/LogicalConfiguration.cpp
//       src/logic/LogicController.cpp
//       src/logic/FakeHardware.cpp
//       src/logic/test/SntpRtcHostTest.cpp -o sntp_rtc_test.exe
//   .\sntp_rtc_test.exe
//
// 未定义 LOGIC_HOST_TEST 时本文件编译为空，不影响固件构建。
//

#if defined(LOGIC_HOST_TEST)

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "communication/domain/SntpTimeParser.h"
#include "configuration/LogicalConfiguration.h"
#include "configuration/RtcClock.h"
#include "logic/FakeHardware.h"
#include "logic/LogicController.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void checkImpl(bool ok, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(cond) checkImpl((cond), #cond, __FILE__, __LINE__)

constexpr uint32_t kSecondMs = 1000UL;
constexpr uint32_t kMinuteMs = 60UL * kSecondMs;
constexpr uint32_t kHourMs = 60UL * kMinuteMs;

// ---------------------------------------------------------------- SntpTimeParser

void testSntpParserValidFormats()
{
    RtcDateTime dt = {};

    // 无冒号后空格的标准 +CIPSNTPTIME: 响应
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:32 2026", &dt));
    CHECK(dt.year == 2026U);
    CHECK(dt.month == 12U);
    CHECK(dt.day == 12U);
    CHECK(dt.hour == 2U);
    CHECK(dt.minute == 33U);
    CHECK(dt.second == 32U);
    CHECK(dt.weekday == 1U); // Mon

    // 冒号后带空格 + 空格填充单位数日期 + CRLF
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME: Thu Jan  1 00:00:00 2026\r\n", &dt));
    CHECK(dt.year == 2026U);
    CHECK(dt.month == 1U);
    CHECK(dt.day == 1U);
    CHECK(dt.hour == 0U);
    CHECK(dt.minute == 0U);
    CHECK(dt.second == 0U);
    CHECK(dt.weekday == 4U); // Thu

    // 空格填充单位数日期 Dec  2 + 行尾空白
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME:Wed Dec  2 23:59:59 2026  \r\n", &dt));
    CHECK(dt.year == 2026U);
    CHECK(dt.month == 12U);
    CHECK(dt.day == 2U);
    CHECK(dt.hour == 23U);
    CHECK(dt.minute == 59U);
    CHECK(dt.second == 59U);
    CHECK(dt.weekday == 3U); // Wed

    // 裸 asctime 字符串（含闰日与零填充单位数日期）
    CHECK(SntpTimeParser::parseLine("Sun Feb 29 12:05:09 2024", &dt));
    CHECK(dt.year == 2024U);
    CHECK(dt.month == 2U);
    CHECK(dt.day == 29U);
    CHECK(dt.hour == 12U);
    CHECK(dt.minute == 5U);
    CHECK(dt.second == 9U);
    CHECK(dt.weekday == 0U); // Sun

    CHECK(SntpTimeParser::parseLine("Mon Jan 05 03:04:05 2026", &dt));
    CHECK((dt.year == 2026U) && (dt.month == 1U) && (dt.day == 5U));
    CHECK((dt.hour == 3U) && (dt.minute == 4U) && (dt.second == 5U));
    CHECK(dt.weekday == 1U); // Mon

    // 400 年闰年 2400-02-29
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME:Tue Feb 29 08:30:00 2400", &dt));
    CHECK(dt.year == 2400U);
    CHECK(dt.month == 2U);
    CHECK(dt.day == 29U);
    CHECK(dt.weekday == 2U); // Tue
}

void testSntpParserRejectsInvalidAnd1970()
{
    const RtcDateTime sentinel = {2026U, 11U, 21U, 19U, 10U, 0U, 6U};
    RtcDateTime dt = sentinel;

    // 1970（未完成 NTP 同步的典型响应）与 2024 之前的年份必须拒绝且不修改输出
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Thu Jan  1 00:00:00 1970", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Thu Jan  1 08:00:00 1970", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:32 2016", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Sun Dec 31 23:59:59 2023", &dt));
    CHECK((dt.year == sentinel.year) && (dt.month == sentinel.month) &&
          (dt.day == sentinel.day) && (dt.hour == sentinel.hour));

    // 空指针与格式错误
    CHECK(!SntpTimeParser::parseLine(nullptr, &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:32 2026", nullptr));
    CHECK(!SntpTimeParser::parseLine("", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSTATUS:Mon Dec 12 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:32 2026 extra", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:32 202", &dt));

    // 非法星期与月份
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Foo Dec 12 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:mon Dec 12 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Xxx 12 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon dec 12 02:33:32 2026", &dt));

    // 非法日期与闰年规则
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec  0 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 32 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 123 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Thu Apr 31 02:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Sat Feb 29 02:33:32 2025", &dt)); // 平年
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Feb 29 02:33:32 2100", &dt)); // 百年非闰

    // 非法时分秒
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 2:33:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 24:00:00 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:60:32 2026", &dt));
    CHECK(!SntpTimeParser::parseLine("+CIPSNTPTIME:Mon Dec 12 02:33:60 2026", &dt));
    CHECK((dt.year == sentinel.year) && (dt.second == sentinel.second));
}

// ---------------------------------------------------------------- RtcClock

void testRtcClockTimeOfDayAndWraparound()
{
    RtcClock::reset();
    CHECK(!RtcClock::isSynced());
    CHECK(RtcClock::version() == 0UL);

    uint32_t msOfDay = 123UL;
    RtcDateTime snap = {};
    CHECK(!RtcClock::readTimeOfDayMs(1000UL, &msOfDay));
    CHECK(!RtcClock::readTimeOfDayMs(1000UL, nullptr));
    CHECK(!RtcClock::snapshot(1000UL, &snap));
    CHECK(!RtcClock::snapshot(1000UL, nullptr));

    // 23:59:50同步，跨零点 +15 s -> 00:00:05 (5000 ms)
    const RtcDateTime lateNight = {2026U, 11U, 21U, 23U, 59U, 50U, 6U};
    CHECK(RtcClock::updateFromSntp(lateNight, 10000UL));
    CHECK(RtcClock::isSynced());
    CHECK(RtcClock::version() == 1UL);

    CHECK(RtcClock::readTimeOfDayMs(10000UL, &msOfDay));
    CHECK(msOfDay == ((23UL * 3600UL + 59UL * 60UL + 50UL) * 1000UL));

    CHECK(RtcClock::readTimeOfDayMs(25000UL, &msOfDay));
    CHECK(msOfDay == 5000UL);

    // 再次校时，版本号单调递增
    const RtcDateTime noon = {2026U, 11U, 22U, 12U, 0U, 0U, 0U};
    CHECK(RtcClock::updateFromSntp(noon, 30000UL));
    CHECK(RtcClock::version() == 2UL);
    CHECK(RtcClock::readTimeOfDayMs(35000UL, &msOfDay));
    CHECK(msOfDay == (12UL * kHourMs + 5000UL));
}

void testRtcClockCalendarRollover()
{
    RtcClock::reset();
    RtcDateTime snap = {};

    // 闰年 2024-02-28 (Wed=3) 23:59:55 + 10 s -> 2024-02-29 (Thu=4) 00:00:05
    const RtcDateTime leapBefore = {2024U, 2U, 28U, 23U, 59U, 55U, 3U};
    CHECK(RtcClock::updateFromSntp(leapBefore, 1000UL));
    CHECK(RtcClock::snapshot(11000UL, &snap));
    CHECK((snap.year == 2024U) && (snap.month == 2U) && (snap.day == 29U));
    CHECK((snap.hour == 0U) && (snap.minute == 0U) && (snap.second == 5U));
    CHECK(snap.weekday == 4U);

    // 再加 1 天 -> 2024-03-01 (Fri=5) 00:00:05
    CHECK(RtcClock::snapshot(11000UL + 86400000UL, &snap));
    CHECK((snap.year == 2024U) && (snap.month == 3U) && (snap.day == 1U));
    CHECK(snap.weekday == 5U);

    // 平年 2025-02-28 (Fri=5) 23:59:55 + 10 s -> 2025-03-01 (Sat=6)
    const RtcDateTime nonLeap = {2025U, 2U, 28U, 23U, 59U, 55U, 5U};
    CHECK(RtcClock::updateFromSntp(nonLeap, 2000UL));
    CHECK(RtcClock::snapshot(12000UL, &snap));
    CHECK((snap.year == 2025U) && (snap.month == 3U) && (snap.day == 1U));
    CHECK(snap.weekday == 6U);

    // 跨年 2026-12-31 (Thu=4) 23:59:55 + 10 s -> 2027-01-01 (Fri=5)
    const RtcDateTime yearEnd = {2026U, 12U, 31U, 23U, 59U, 55U, 4U};
    CHECK(RtcClock::updateFromSntp(yearEnd, 5000UL));
    CHECK(RtcClock::snapshot(15000UL, &snap));
    CHECK((snap.year == 2027U) && (snap.month == 1U) && (snap.day == 1U));
    CHECK((snap.hour == 0U) && (snap.minute == 0U) && (snap.second == 5U));
    CHECK(snap.weekday == 5U);

    // 多天跨越（40 天）：2026-01-01 01:02:03 + 40 d -> 2026-02-10 01:02:03
    const RtcDateTime jan1 = {2026U, 1U, 1U, 1U, 2U, 3U, 4U};
    CHECK(RtcClock::updateFromSntp(jan1, 0UL));
    CHECK(RtcClock::snapshot(40UL * 86400000UL, &snap));
    CHECK((snap.year == 2026U) && (snap.month == 2U) && (snap.day == 10U));
    CHECK((snap.hour == 1U) && (snap.minute == 2U) && (snap.second == 3U));
    CHECK(snap.weekday == ((4U + 40U) % 7U));
}

void testRtcClockUptimeWraparoundAndInvalidRejection()
{
    RtcClock::reset();

    // uint32 uptime 从 0xFFFFFC00UL 回绕到 0x000003E8UL（经过 2024 ms）
    const RtcDateTime base = {2026U, 11U, 21U, 23U, 59U, 59U, 6U};
    const uint32_t syncTick = 0xFFFFFC00UL;
    const uint32_t nowTick = 0x000003E8UL; // nowTick - syncTick == 2024 ms
    CHECK(RtcClock::updateFromSntp(base, syncTick));
    const uint32_t expectedVer = RtcClock::version();

    uint32_t msOfDay = 0UL;
    CHECK(RtcClock::readTimeOfDayMs(nowTick, &msOfDay));
    CHECK(msOfDay == 1024UL); // 23:59:59.000 + 2.024 s = 00:00:01.024

    RtcDateTime snap = {};
    CHECK(RtcClock::snapshot(nowTick, &snap));
    CHECK((snap.year == 2026U) && (snap.month == 11U) && (snap.day == 22U));
    CHECK((snap.hour == 0U) && (snap.minute == 0U) && (snap.second == 1U));
    CHECK(snap.weekday == 0U); // Sun

    // 非法输入不得覆盖既有基准或递增版本
    const RtcDateTime badDates[] = {
        {1970U, 1U, 1U, 0U, 0U, 0U, 4U},
        {2023U, 12U, 31U, 23U, 59U, 59U, 0U},
        {2026U, 0U, 1U, 0U, 0U, 0U, 0U},
        {2026U, 13U, 1U, 0U, 0U, 0U, 0U},
        {2026U, 4U, 31U, 0U, 0U, 0U, 0U},
        {2025U, 2U, 29U, 0U, 0U, 0U, 0U},
        {2026U, 1U, 1U, 24U, 0U, 0U, 0U},
        {2026U, 1U, 1U, 0U, 60U, 0U, 0U},
        {2026U, 1U, 1U, 0U, 0U, 60U, 0U},
        {2026U, 1U, 1U, 0U, 0U, 0U, 7U},
    };
    for (std::size_t i = 0U; i < (sizeof(badDates) / sizeof(badDates[0])); ++i)
    {
        CHECK(!RtcClock::updateFromSntp(badDates[i], 100UL));
    }
    CHECK(RtcClock::version() == expectedVer);
    CHECK(RtcClock::isSynced());
}

// ---------------------------------------------------------------- LogicController 集成

void testLogicControllerIntegrationWithRtcAndConfig()
{
    RtcClock::reset();

    LogicalStatics cfg = {};
    LogicalConfiguration::snapshot(&cfg);
    CHECK(cfg.fixed.hasRtc);

    greenhouse::FakeHardware hw;
    hw.temperature = 24.0F;
    hw.air_humidity = 55.0F;
    hw.soil_humidity = 50.0F;
    hw.illuminance = 100U; // 低于默认日间补光阈值

    greenhouse::LogicController ctrl(hw, cfg);
    uint32_t lastRtcVersion = 0UL;
    uint32_t nowMs = 1000UL;

    // 未校时前：hasRtc = true 但尚未 setTimeOfDay -> 报 FAULT_TIME_UNSET
    ctrl.step(nowMs);
    CHECK((ctrl.snapshot().faults & greenhouse::FAULT_TIME_UNSET) != 0U);
    CHECK(!ctrl.snapshot().time_valid);

    // 模拟通信线程收到白天 SNTP 响应（10:00:00，在默认 07:00~17:00 昼间窗口内）
    RtcDateTime sntpDt = {};
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME:Sat Nov 21 10:00:00 2026", &sntpDt));
    CHECK(RtcClock::updateFromSntp(sntpDt, nowMs));

    // 模拟 new_thread0_entry 同步逻辑
    nowMs += 100UL;
    if (RtcClock::version() != lastRtcVersion)
    {
        uint32_t msOfDay = 0UL;
        CHECK(RtcClock::readTimeOfDayMs(nowMs, &msOfDay));
        lastRtcVersion = RtcClock::version();
        ctrl.setTimeOfDay(msOfDay);
    }

    ctrl.step(nowMs);
    CHECK((ctrl.snapshot().faults & greenhouse::FAULT_TIME_UNSET) == 0U);
    CHECK(ctrl.snapshot().time_valid);
    CHECK(ctrl.snapshot().light_on); // 昼间弱光 -> 开补光灯

    // 超过最短点亮时间（默认 10 min）后，重新校时到夜间（22:00:00）
    nowMs += 11UL * 60UL * kSecondMs;
    CHECK(SntpTimeParser::parseLine("+CIPSNTPTIME:Sat Nov 21 22:00:00 2026", &sntpDt));
    CHECK(RtcClock::updateFromSntp(sntpDt, nowMs));

    nowMs += 100UL;
    if (RtcClock::version() != lastRtcVersion)
    {
        uint32_t msOfDay = 0UL;
        CHECK(RtcClock::readTimeOfDayMs(nowMs, &msOfDay));
        lastRtcVersion = RtcClock::version();
        ctrl.setTimeOfDay(msOfDay);
    }

    ctrl.step(nowMs);
    CHECK(ctrl.snapshot().time_valid);
    CHECK(!ctrl.snapshot().light_on); // 夜间即使光照低也关闭补光灯
}

void testRtcClockFormatLogTime()
{
    RtcClock::reset();

    char buf[RtcClock::kLogTimeBufferSize] = {};
    char smallBuf[8] = {'x', 'y', '\0'};
    CHECK(!RtcClock::formatLogTime(0UL, nullptr, sizeof(buf)));
    CHECK(!RtcClock::formatLogTime(0UL, smallBuf, sizeof(smallBuf)));
    CHECK(smallBuf[0] == '\0');

    CHECK(RtcClock::formatLogTime(0UL, buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "0 ms") == 0);
    CHECK(std::strcmp(RtcClock::logTime(12345UL).text, "12345 ms") == 0);

    const RtcDateTime base = {2026U, 11U, 21U, 23U, 59U, 59U, 6U};
    CHECK(RtcClock::updateFromSntp(base, 1000UL));

    CHECK(RtcClock::formatLogTime(1005UL, buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "2026-11-21 23:59:59.005") == 0);
    CHECK(std::strcmp(RtcClock::logTime(2500UL).text, "2026-11-22 00:00:00.500") == 0);
}

} // namespace

int main()
{
    testSntpParserValidFormats();
    testSntpParserRejectsInvalidAnd1970();
    testRtcClockTimeOfDayAndWraparound();
    testRtcClockCalendarRollover();
    testRtcClockUptimeWraparoundAndInvalidRejection();
    testLogicControllerIntegrationWithRtcAndConfig();
    testRtcClockFormatLogTime();

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return (0 == g_failures) ? 0 : 1;
}

#endif // LOGIC_HOST_TEST
