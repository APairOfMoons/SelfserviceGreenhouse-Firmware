//
// 纯整数的公历换算与 asctime 解析。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_CIVILTIME_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_CIVILTIME_H

#include <cstdint>


/**
 * 公历换算与 asctime 解析。
 *
 * 不调用 libc 的 time/gmtime/localtime：那套要 locale 和时区数据库，部分实现还会
 * 分配内存。这里只有一次 SNTP 读数要处理，输入是模块回的一行 asctime。
 *
 * 逻辑模块只要"本地当天已过的毫秒数"，不需要日期，所以这里不提供 tm 转换，
 * 只把 UTC 秒折算成当天毫秒（跨日由取模处理）。
 */
class CivilTime
{
    public:
    /**
     * 解析 asctime 风格的时间串："Www Mmm dd hh:mm:ss yyyy"。
     *
     * 固定 24 字符、英文缩写、日左补空格（"Dec  2"）。只做语法与字段范围校验，
     * **不判断时间是否合理**——1970 年也能通过，下限由调用方决定。
     *
     * @param text         以星期缩写开头的字符串
     * @param epochSeconds 输出：UTC Unix 秒
     * @return 格式非法、字段越界或早于 1970 时返回 false
     */
    static bool parseAsctime(const char *text, uint32_t *epochSeconds);

    /**
     * UTC Unix 秒 → 本地当天已过的毫秒数，范围 [0, 86400000)。
     *
     * @param offsetMinutes 相对 UTC 的偏移，东为正（东八区 = 480）
     */
    static uint32_t timeOfDayMs(uint32_t epochSeconds, int32_t offsetMinutes);

    /** 公历年月日 → 距 1970-01-01 的天数（可为负）。 */
    static int32_t daysFromCivil(int year, unsigned month, unsigned day);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_CIVILTIME_H
