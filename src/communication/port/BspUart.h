//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_BSPUART_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_BSPUART_H

#include <cstdint>

/**
 * SCI0 上的串口驱动，对端是 ESP-01s（P100 = RXD0，P101 = TXD0）。
 *
 * 只做三件事：打开串口、成块发送、把收到的字节放进环形缓冲。
 * 不解析内容、不认识 AT 指令——那是 transport 层的事。
 *
 * 线程约定：所有函数只在通信线程里调用，中断只经由 uart0_callback 写缓冲。
 * 接收方向上 head 由中断单写、tail 由任务单写，都是 32 位对齐变量，
 * 单核 Cortex-M33 上不需要临界区；发送方向的 txIdle 同理（中断置位、任务清零）。
 * 一个任务独占一条串口，是 configUSE_MUTEXES=0 下的必要前提（约束 C7）。
 */
class BspUart
{
    public:
    /** 打开 SCI0。重复调用安全：已打开时直接返回 true。 */
    static bool initialize();

    /** 串口是否已经成功打开。 */
    static bool isReady();

    /**
     * 成块发送，阻塞到整块发完或超时。
     *
     * 115200 8N1 下 256 字节约 22 ms、1 KB 约 89 ms，超时值按这个量级取。
     * 发送本身是中断驱动的，等待期间会让出 CPU，不会饿死低优先级任务。
     *
     * @return 整块发送完成返回 true；参数非法、驱动报错或超时返回 false
     */
    static bool write(const uint8_t *data, uint32_t length, uint32_t timeoutMs);

    /** 取一个字节。缓冲为空时返回 false，不阻塞。 */
    static bool readByte(uint8_t *value);

    /** 批量取字节，最多 capacity 个；返回实际取到的个数。 */
    static uint32_t read(uint8_t *buffer, uint32_t capacity);

    /** 当前可读字节数。 */
    static uint32_t available();

    /** 丢弃缓冲里尚未读走的内容（行解析重新同步时用）。 */
    static void flushReceive();

    /** 因环形缓冲满而丢掉的字节数。非 0 说明上层读得太慢。 */
    static uint32_t droppedCount();

    /** 串口错误事件（溢出、帧错误、校验错）次数。 */
    static uint32_t errorCount();

    /** 环形接收缓冲的容量，单位为字节。 */
    static uint32_t ringCapacity();
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_BSPUART_H
