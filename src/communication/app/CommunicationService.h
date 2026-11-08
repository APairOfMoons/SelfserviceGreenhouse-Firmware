//
// Created by APairOfMoons on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_COMMUNICATIONSERVICE_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_COMMUNICATIONSERVICE_H

#include <cstdint>


/**
 * 通信线程的主体：队列、状态机、重连、配置分发。
 *
 * 线程模型（设计文档 §6.3）：comm_thread 独占串口与 AT 通道，逻辑线程只通过
 * submit() 入队，不阻塞、不碰串口。所有 RTOS 调用都经 port/CommRtos 的 C 接口。
 *
 * 生命周期：initialize() 在 run() 的第一条语句里调用（因为 ra_gen/main.c 是
 * 生成的，没有用户钩子可以插在 vTaskStartScheduler() 之前）；submit() 在队列
 * 建立之前返回 false，这个窗口只有毫秒级。
 */
class CommunicationService
{
    public:
    /** 一条待发送的报文。定长，按值入队。 */
    struct OutgoingMessage
    {
        char topic[64];
        char payload[224];
        uint16_t length;
        uint8_t qos;
        bool retain;
    };

    enum class State : uint8_t
    {
        kBooting,          ///< 刚上电，等模块稳定
        kModuleReady,      ///< AT 通，正在关回显、设 Station
        kWifiReady,        ///< 等 IP
        kMqttConnecting,   ///< 已配置，正在连 broker
        kOnline,           ///< 收发正常
        kReconnecting,     ///< 断线后退避
        kFault,            ///< 连续失败，准备硬复位模块
    };

    /** 建队列、打开串口。重复调用返回 true。 */
    static bool initialize();

    /** 线程主循环，不返回。 */
    static void run();

    /**
     * 非阻塞入队。
     *
     * @return 队列未建立、报文非法或已满时返回 false；调用方不必重试
     */
    static bool submit(const OutgoingMessage &message);

    /** 当前状态，只读。 */
    static State state();

    /** 已成功交给模块的报文数。 */
    static uint32_t sentCount();

    /** 因队列满而丢掉的报文数。 */
    static uint32_t droppedCount();

    /** 模块拒绝或应答超时的报文数。 */
    static uint32_t failedCount();
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_COMMUNICATIONSERVICE_H
