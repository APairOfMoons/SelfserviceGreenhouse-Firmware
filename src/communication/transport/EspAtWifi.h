//
// Created by APairOfMoons on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ESPATWIFI_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ESPATWIFI_H

#include <cstdint>


/**
 * Wi-Fi 相关的 AT 命令。
 *
 * 只是 AT 序列的封装，不保存任何状态：处于什么阶段由上层状态机决定。
 * 每个函数成功返回 true，失败（ERROR／FAIL／超时）返回 false，
 * 具体原因由调用方按自己的策略处理（重试、退避、复位）。
 */
class EspAtWifi
{
    public:
    /** AT，用于确认模块在线。 */
    static bool ping(uint32_t timeoutMs);

    /** ATE0，关闭回显。复位后需要重发。 */
    static bool disableEcho(uint32_t timeoutMs);

    /** AT+CWMODE=1，设成 Station。 */
    static bool setStationMode(uint32_t timeoutMs);

    /** AT+CWAUTOCONN=1，让模块自己重连已保存的 AP。 */
    static bool enableAutoConnect(uint32_t timeoutMs);

    /** AT+CWJAP，连接指定的 AP。 */
    static bool join(const char *ssid, const char *password, uint32_t timeoutMs);

    /**
     * AT+CIPSTATUS，判断是否已拿到 IP。
     *
     * STATUS:2（已获 IP）或 3（已建立 TCP）都算就绪；0/1/4/5 不算。
     */
    static bool isNetworkReady(uint32_t timeoutMs);

    /** AT+RST，硬复位模块。 */
    static bool reset(uint32_t timeoutMs);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ESPATWIFI_H
