//
// SNTP 相关的 AT 命令。
//

#include "EspAtSntp.h"

#include <cstddef>
#include <cstring>

#include "AtChannel.h"
#include "communication/domain/CivilTime.h"


extern "C"
{
volatile uint8_t g_sntpLastResult = 0xFFU;
char g_sntpLastLine[AT_LINE_MAX] = {0};
volatile uint32_t g_sntpQueryFails = 0U;
}


namespace
{

/*
 * 固定 UTC（第二个参数 0），服务器用实测可达的两台：国家授时中心与上交大
 * 网络中心。不省略服务器时模块会用内置的 cn.ntp.org.cn / ntp.sjtu.edu.cn /
 * us.pool.ntp.org，其中第三台在境外，国内不一定快。
 *
 * 整条 52 B，离 §4.5 说的未知长度上限还有余量。
 */
constexpr const char *kConfigureCommand =
    "AT+CIPSNTPCFG=1,0,\"ntp.ntsc.ac.cn\",\"ntp.sjtu.edu.cn\"";

constexpr const char *kQueryCommand = "AT+CIPSNTPTIME?";
constexpr const char *kQueryPrefix = "+CIPSNTPTIME:";
constexpr std::size_t kQueryPrefixLength = 13U;

/*
 * 2024-01-01T00:00:00Z。早于它的只可能是"还没同步上"的 1970；
 * 拿它当有效性下限，比单纯看"年份是否大于 1970"稳。
 */
constexpr uint32_t kEarliestSaneEpoch = 1704067200U;


void rememberLine(const char *line)
{
    std::memset(g_sntpLastLine, 0, sizeof(g_sntpLastLine));

    if (line == nullptr)
    {
        return;
    }

    const std::size_t length = std::strlen(line);
    const std::size_t limit = (length < (sizeof(g_sntpLastLine) - 1U))
                                  ? length
                                  : (sizeof(g_sntpLastLine) - 1U);
    std::memcpy(g_sntpLastLine, line, limit);
}


SntpResult record(SntpResult result)
{
    g_sntpLastResult = static_cast<uint8_t>(result);
    return result;
}

} // namespace


SntpResult EspAtSntp::configureUtc(uint32_t timeoutMs)
{
    const AtResult result = AtChannel::execute(kConfigureCommand, timeoutMs);
    rememberLine(result.line);

    if (result.status == AtStatus::kOk)
    {
        return record(SntpResult::kOk);
    }

    if (result.status == AtStatus::kError)
    {
        return record(SntpResult::kUnsupported);
    }

    return record(SntpResult::kFailed);
}


SntpResult EspAtSntp::queryUtc(uint32_t *epochSeconds, uint32_t timeoutMs)
{
    const AtResult result = AtChannel::execute(kQueryCommand, timeoutMs);
    rememberLine(result.line);

    if (result.status == AtStatus::kError)
    {
        return record(SntpResult::kUnsupported);
    }

    if (result.status != AtStatus::kOk)
    {
        ++g_sntpQueryFails;
        return record(SntpResult::kFailed);
    }

    if (std::strncmp(result.line, kQueryPrefix, kQueryPrefixLength) != 0)
    {
        ++g_sntpQueryFails;
        return record(SntpResult::kFailed);
    }

    uint32_t epoch = 0U;
    if (!CivilTime::parseAsctime(result.line + kQueryPrefixLength, &epoch))
    {
        ++g_sntpQueryFails;
        return record(SntpResult::kFailed);
    }

    if (epoch < kEarliestSaneEpoch)
    {
        /* 格式对、年份是 1970：模块还没完成一次 NTP 往返，等一会儿再读 */
        ++g_sntpQueryFails;
        return record(SntpResult::kNotReady);
    }

    if (epochSeconds != nullptr)
    {
        *epochSeconds = epoch;
    }

    return record(SntpResult::kOk);
}
