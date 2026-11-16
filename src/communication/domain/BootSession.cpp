//
// Created by APairOfMoons on 2026/11/14.
//

#include "BootSession.h"

#include "TextWriter.h"


/*
 * 调试器可见的诊断量，与 CommunicationService 里的 g_comm* 一样用 C 链接。
 * 它们只供观察，不参与任何逻辑判断。
 */
extern "C"
{
volatile uint32_t g_bootNonce    = 0U;   /* 本次上电的标识，0 表示尚未生成 */
char              g_bootNonceHex[9] = {0};  /* 同上，8 位小写十六进制 */
}


namespace
{

uint32_t s_nonce = 0U;

} // namespace


uint32_t BootSession::mix(uint32_t systickPhase, uint32_t uptimeMs, uint32_t stackAddress)
{
    /*
     * 三份输入各乘一个大奇数再异或，把微小的差异摊到整个 32 位上。
     * 常数取黄金比例与 murmur3 的终混常量，只求扩散快，没有别的讲究。
     */
    uint32_t value = systickPhase;
    value ^= (uptimeMs * 0x9E3779B9U);
    value ^= (stackAddress * 0x85EBCA6BU);

    /* xorshift32 的初值不能为 0，补一个常量兜底 */
    value ^= 0xA5A5A5A5U;

    for (uint32_t round = 0U; round < 16U; ++round)
    {
        value ^= (value << 13U);
        value ^= (value >> 17U);
        value ^= (value << 5U);
    }

    return (value == 0U) ? 1U : value;
}


void BootSession::establish(uint32_t nonce)
{
    if (s_nonce != 0U)
    {
        return;
    }

    s_nonce = (nonce == 0U) ? 1U : nonce;
    g_bootNonce = s_nonce;

    TextWriter writer(g_bootNonceHex, sizeof(g_bootNonceHex));
    (void) writer.appendHex32(s_nonce);
}


uint32_t BootSession::nonce()
{
    return s_nonce;
}


bool BootSession::format(char *buffer, std::size_t capacity)
{
    if ((buffer == nullptr) || (capacity == 0U))
    {
        return false;
    }

    buffer[0] = '\0';

    if (s_nonce == 0U)
    {
        return false;
    }

    TextWriter writer(buffer, capacity);
    if (!writer.appendHex32(s_nonce) || writer.isTruncated())
    {
        buffer[0] = '\0';
        return false;
    }

    return true;
}
