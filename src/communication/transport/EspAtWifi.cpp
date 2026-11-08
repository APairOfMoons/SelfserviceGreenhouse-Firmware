//
// Created by APairOfMoons on 2026/11/8.
//

#include "EspAtWifi.h"

#include "AtChannel.h"
#include "communication/domain/TextWriter.h"


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
    return result.status == AtStatus::kOk;
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
        return false;
    }

    /* "STATUS:<stat>"，2 = 已获 IP，3 = 已建立 TCP */
    const char state = line[7];
    return (state == '2') || (state == '3');
}


bool EspAtWifi::reset(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+RST", timeoutMs).status == AtStatus::kOk;
}
