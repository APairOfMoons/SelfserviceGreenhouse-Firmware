//
// SNTP 相关的 AT 命令。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ESPATSNTP_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ESPATSNTP_H

#include <cstdint>


/** `AT+CIPSNTPTIME?` 的结果。 */
enum class SntpResult : uint8_t
{
    kOk = 0,           ///< 读到有效时间
    kNotReady = 1,     ///< 读到了，但年份早于 2024：模块还没同步上
    kUnsupported = 2,  ///< 模块回 ERROR：固件没编 SNTP，或命令格式不对
    kFailed = 3,       ///< 超时，或回包不是可解析的 asctime
};


/*
 * 失败现场，供调试器查看。g_sntpLastLine 是模块回的最后一行，未就绪时它能
 * 直接显示 "Thu Jan  1 00:00:00 1970"，比只看返回值直观。
 */
extern "C"
{
extern volatile uint8_t g_sntpLastResult;  /* SntpResult */
extern char g_sntpLastLine[];
extern volatile uint32_t g_sntpQueryFails;
}


/**
 * SNTP 的 AT 封装：只做序列与解析，不留状态。
 *
 * 时间统一按 **UTC** 取：`AT+CIPSNTPTIME?` 返回的字符串里没有时区字段，
 * 让模块自己加偏移的话，一旦 `CIPSNTPCFG` 没生效就没法从字面上分辨（见
 * docs/AT_COMMAND_SET.md §4.6）。本地偏移由上层加。
 */
class EspAtSntp
{
    public:
    /**
     * `AT+CIPSNTPCFG=1,0,"ntp.ntsc.ac.cn","ntp.sjtu.edu.cn"`。
     *
     * 只改模块本地配置、不走网络，但**掉电/复位后丢失**，每次尝试校时前都要重发。
     *
     * @return kOk；kUnsupported（回 ERROR，说明固件没编 SNTP，不必再试）；
     *         kFailed（超时等，下次还能试）
     */
    static SntpResult configureUtc(uint32_t timeoutMs);

    /**
     * `AT+CIPSNTPTIME?`，把 asctime 解析成 UTC Unix 秒。
     *
     * @param epochSeconds 输出，只在返回 kOk 时写入
     */
    static SntpResult queryUtc(uint32_t *epochSeconds, uint32_t timeoutMs);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ESPATSNTP_H
