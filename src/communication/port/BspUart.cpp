//
// Created by APairOfMoons on 2026/11/3.
//

#include "BspUart.h"

#include "CommRtos.h"

/*
 * 必须先包含 hal_data.h：FSP 的头文件以 bsp_api.h 为根，若从
 * fsp_common_api.h 起步会撞上它和 fsp_version.h 的循环包含，
 * FSP_HEADER 此时尚未定义，整棵 BSP 头文件都会报错。
 */
#include "hal_data.h"
#include "r_sci_uart.h"


/*
 * 调试器可见的串口累计量。
 *
 * 文件内的 s_rxDropped / s_rxErrors 是内部链接符号，调试器里名字被修饰过，
 * 不方便按名字查；这里单开一组 C 链接计数专供观察。
 *
 * g_uartRxBytes 是判断"模块到底有没有回话"的唯一直接证据：发送成功只说明
 * 单片机把字节推进了 SCI0，收到字节才说明线序、电平、波特率都对得上。
 */
extern "C"
{
volatile uint32_t g_uartRxBytes = 0U;
volatile uint32_t g_uartErrors  = 0U;
}


namespace
{

/*
 * 环形接收缓冲 512 B。115200 8N1 下 512 B 约 44 ms，远大于通信线程
 * 处理一条报文的时间，正常情况下不会丢字节。
 */
constexpr uint32_t kRxRingSize = 512U;
constexpr uint32_t kRxRingMask = kRxRingSize - 1U;

/* 等待发送完成时的轮询粒度。tick 为 1 ms，这个粒度对 115200 足够。 */
constexpr uint32_t kTxPollMs = 1U;

/* 等上一次发送收尾的上限。正常情况下空闲标志早已置位，这只是防御。 */
constexpr uint32_t kTxSettleTimeoutMs = 100U;

uint8_t s_rxRing[kRxRingSize];

volatile uint32_t s_rxHead = 0U;      /* 中断写，任务只读 */
volatile uint32_t s_rxTail = 0U;      /* 任务写，中断只读 */
volatile uint32_t s_rxDropped = 0U;   /* 缓冲满时丢弃的字节数 */
volatile uint32_t s_rxErrors = 0U;    /* 溢出／帧错误／校验错 */
volatile bool s_txIdle = true;        /* 中断置 true，任务置 false */

bool s_ready = false;

/* 轮询等待整块发送完成。发送是中断驱动的，这里只要让出 CPU 即可。 */
bool waitTxIdle(uint32_t timeoutMs)
{
    for (uint32_t elapsed = 0U; elapsed <= timeoutMs; elapsed += kTxPollMs)
    {
        if (s_txIdle)
        {
            return true;
        }

        commRtosDelay(kTxPollMs);
    }

    return s_txIdle;
}

} // namespace


/*
 * SCI0 的接收完成／发送完成中断回调。
 *
 * 必须是 C 链接：声明来自 FSP 生成的 hal_data.h（在 extern "C" 里），
 * 名字不经 C++ 修饰，否则链接时找不到这个符号。
 *
 * 这里只搬字节，不解析、不打印、不用堆。
 */
extern "C" void uart0_callback(uart_callback_args_t *p_args)
{
    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
        {
            g_uartRxBytes = g_uartRxBytes + 1U;

            const uint32_t next = (s_rxHead + 1U) & kRxRingMask;
            if (next != s_rxTail)
            {
                s_rxRing[s_rxHead] = static_cast<uint8_t>(p_args->data);
                s_rxHead = next;
            }
            else
            {
                /* 缓冲满：丢掉新字节而不覆盖旧字节，已收到的半条报文仍可解析 */
                s_rxDropped = s_rxDropped + 1U;
            }
            break;
        }

        case UART_EVENT_TX_COMPLETE:
        {
            s_txIdle = true;
            break;
        }

        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_PARITY:
        {
            g_uartErrors = g_uartErrors + 1U;
            s_rxErrors = s_rxErrors + 1U;
            break;
        }

        default:
        {
            break;
        }
    }
}


bool BspUart::initialize()
{
    if (s_ready)
    {
        return true;
    }

    s_rxHead = 0U;
    s_rxTail = 0U;
    s_rxDropped = 0U;
    s_rxErrors = 0U;
    s_txIdle = true;
    g_uartRxBytes = 0U;
    g_uartErrors = 0U;

    const fsp_err_t result = R_SCI_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    if (result != FSP_SUCCESS)
    {
        return false;
    }

    s_ready = true;
    return true;
}

bool BspUart::isReady()
{
    return s_ready;
}

bool BspUart::write(const uint8_t *data, uint32_t length, uint32_t timeoutMs)
{
    if ((!s_ready) || (data == nullptr) || (length == 0U))
    {
        return false;
    }

    /* 驱动在发送进行中会返回 FSP_ERR_IN_USE，先等上一次收尾 */
    if (!waitTxIdle(kTxSettleTimeoutMs))
    {
        return false;
    }

    s_txIdle = false;

    if (R_SCI_UART_Write(&g_uart0_ctrl, data, length) != FSP_SUCCESS)
    {
        s_txIdle = true;
        return false;
    }

    return waitTxIdle(timeoutMs);
}

bool BspUart::readByte(uint8_t *value)
{
    if (value == nullptr)
    {
        return false;
    }

    if (s_rxTail == s_rxHead)
    {
        return false;
    }

    *value = s_rxRing[s_rxTail];
    s_rxTail = (s_rxTail + 1U) & kRxRingMask;
    return true;
}

uint32_t BspUart::read(uint8_t *buffer, uint32_t capacity)
{
    if ((buffer == nullptr) || (capacity == 0U))
    {
        return 0U;
    }

    uint32_t count = 0U;
    while ((count < capacity) && (s_rxTail != s_rxHead))
    {
        buffer[count] = s_rxRing[s_rxTail];
        s_rxTail = (s_rxTail + 1U) & kRxRingMask;
        ++count;
    }

    return count;
}

uint32_t BspUart::available()
{
    if (s_rxTail == s_rxHead)
    {
        return 0U;
    }

    return ((s_rxHead + kRxRingSize) - s_rxTail) & kRxRingMask;
}

void BspUart::flushReceive()
{
    /* head 只由中断推进；直接把它当前的值取过来作为新的 tail 即可 */
    s_rxTail = s_rxHead;
}

uint32_t BspUart::droppedCount()
{
    return s_rxDropped;
}

uint32_t BspUart::errorCount()
{
    return s_rxErrors;
}

uint32_t BspUart::ringCapacity()
{
    return kRxRingSize;
}
