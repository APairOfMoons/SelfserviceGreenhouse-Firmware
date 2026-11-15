//
// Created by APairOfMoons on 2026/11/14.
//

#include "EntropySource.h"

#include "hal_data.h"


extern "C" uint32_t entropySystickPhase(void)
{
    uint32_t accumulator = 0U;

    for (uint32_t sample = 0U; sample < 32U; ++sample)
    {
        /*
         * 两次读之间只隔几条指令。中断若正好落在这几个周期里，
         * 相邻两次的差值就会变，这就是要采的东西。
         */
        accumulator ^= SysTick->VAL;
        accumulator = (accumulator << 1U) | (accumulator >> 31U);

        __NOP();
        __NOP();
    }

    return accumulator;
}


extern "C" uint32_t entropyStackAddress(void)
{
    volatile uint32_t marker = 0U;

    /* 取局部变量地址的低位；marker 恒为 0，顺带压掉未使用告警 */
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&marker)) ^ marker;
}
