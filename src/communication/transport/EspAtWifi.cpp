//
// Created by APairOfMoons on 2026/11/8.
//

#include "EspAtWifi.h"

#include <cstring>

#include "AtChannel.h"
#include "communication/domain/TextWriter.h"


/*
 * 调试器可见的诊断量（与 AtChannel / CommunicationService 里的同一套路）。
 *
 * 连不上热点时，`AT+CWJAP` 的失败原因只出现在终结行之前的那一行，形如
 * `+CWJAP:<n>`；`join()` 只看终结行 OK/FAIL，若不在这里留存，这条线索
 * 就随 result 一起被丢掉了。没有它只能看到"失败了"，分不清是密码错、
 * 找不到 AP，还是关联被拒。
 *
 * g_wifiStatus 记录 `AT+CIPSTATUS` 的 STATUS:<n>：0=未初始化 1=已连 AP
 * 未取 IP 2=已取 IP 3=已建 TCP 4=连接断开 5=未连 AP。
 */
extern "C"
{
volatile uint8_t g_wifiStatus = 0xFFU;
volatile uint32_t g_wifiJoinFails = 0U;
char g_wifiLastJoinLine[AT_LINE_MAX] = {0};
}


namespace
{

/*
 * 拼命令用的缓冲。单任务独占，放静态区而不是栈上——comm_thread 只有 3 KB 栈，
 * 而这条缓冲要贯穿整个 execute() 调用的栈深度。
 */
char s_commandBuffer[AT_COMMAND_MAX];

} // namespace


bool EspAtWifi::ping(uint32_t timeoutMs)
{
    return AtChannel::execute("AT", timeoutMs).status == AtStatus::kOk;
}


bool EspAtWifi::disableEcho(uint32_t timeoutMs)
{
    return AtChannel::execute("ATE0", timeoutMs).status == AtStatus::kOk;
}


bool EspAtWifi::setStationMode(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+CWMODE=1", timeoutMs).status == AtStatus::kOk;
}


bool EspAtWifi::enableAutoConnect(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+CWAUTOCONN=1", timeoutMs).status == AtStatus::kOk;
}


bool EspAtWifi::join(const char *ssid, const char *password, uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    writer.append("AT+CWJAP=\"");
    writer.append(ssid);
    writer.append("\",\"");
    writer.append(password);
    writer.append('"');

    if (writer.isTruncated())
    {
        return false;
    }

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);

    /*
     * 成功时先出现 WIFI CONNECTED / WIFI GOT IP，这两行按 URC 存槽，
     * 终结行是 OK；密码错误等失败返回 FAIL（不是 ERROR）。
     */
    if (result.status != AtStatus::kOk)
    {
        /* 失败时 result.line 就是 +CWJAP:<n>，留一份给调试器 */
        ++g_wifiJoinFails;
        std::memcpy(g_wifiLastJoinLine, result.line, std::strlen(result.line) + 1U);
        return false;
    }

    return true;
}


bool EspAtWifi::isNetworkReady(uint32_t timeoutMs)
{
    char line[AT_LINE_MAX] = {};

    /*
     * 已连接 TCP 时响应是 "STATUS:3" 再跟 "+CIPSTATUS:..."，
     * 后者会覆盖 execute() 那唯一一格 line，所以用 executeAndWait 专抓前者。
     */
    if (!AtChannel::executeAndWait("AT+CIPSTATUS", "STATUS:", timeoutMs, line, sizeof(line)))
    {
        /* 连 STATUS: 这一行都没等到，模块可能已经不理人了 */
        g_wifiStatus = 0xFFU;
        return false;
    }

    /* "STATUS:<stat>"，2 = 已获 IP，3 = 已建立 TCP */
    const char state = line[7];
    g_wifiStatus = ((state >= '0') && (state <= '9'))
                       ? static_cast<uint8_t>(state - '0')
                       : static_cast<uint8_t>(0xFFU);

    return (state == '2') || (state == '3');
}


bool EspAtWifi::reset(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+RST", timeoutMs).status == AtStatus::kOk;
}
