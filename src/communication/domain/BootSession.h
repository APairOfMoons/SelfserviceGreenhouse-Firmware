//
// Created by APairOfMoons on 2026/11/14.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_BOOTSESSION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_BOOTSESSION_H

#include <cstddef>
#include <cstdint>


/**
 * 本次上电的会话标识，即报文里的 "b"。
 *
 * 设备重启后 seq 与 up 都归零，下游无法区分"新会话的第一条"与"旧会话的重传"。
 * 这个标识每次上电生成一次，同一次上电内不变——MQTT 重连、ESP-01s 被
 * AT+RST 复位都不重新生成，只有 MCU 复位才生成新的。
 *
 * 它不是密钥、不参与认证、不要求统计学随机性；重复的后果只是下游归组错一次。
 */
class BootSession
{
    public:
    /**
     * 把三份熵源混成 32 位标识。
     *
     * 纯函数：同样的输入得到同样的输出，因此可以在主机上做固定向量测试。
     * 任何输入组合都不会返回 0（0 保留给"尚未建立"）。
     */
    static uint32_t mix(uint32_t systickPhase, uint32_t uptimeMs, uint32_t stackAddress);

    /** 记录本次上电的标识。只在第一次调用时生效，之后忽略。 */
    static void establish(uint32_t nonce);

    /** 当前标识；0 表示尚未建立。 */
    static uint32_t nonce();

    /**
     * 写成 8 位小写十六进制加 '\0'（共 9 字节）。
     *
     * @return 未建立或容量不足时返回 false，且 buffer 被置为空串
     */
    static bool format(char *buffer, std::size_t capacity);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_BOOTSESSION_H
