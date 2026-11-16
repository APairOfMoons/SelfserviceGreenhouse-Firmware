//
// Created by APairOfMoons on 2026/11/4.
//

#include "ConfigurationPayload.h"

#include <cstring>

#include "TextWriter.h"


namespace {

constexpr std::size_t kMaxKeyLength = 24;

bool equals(const char *left, const char *right)
{
    return std::strcmp(left, right) == 0;
}

/**
 * 面向固定 schema 的扁平 JSON 解析器。
 *
 * 只支持 { "key": integer, ... }，不支持字符串、布尔、嵌套与转义——
 * 配置报文本来就只有数字。不分配内存，出错即整体失败。
 */
class Parser
{
    public:
    Parser(const char *text, std::size_t length)
        : m_text(text), m_length(length), m_position(0)
    {
    }

    bool parseObject(VariableStatics *inOut)
    {
        skipSpaces();
        if (!consume('{'))
        {
            return false;
        }

        skipSpaces();
        if (consume('}'))
        {
            return true;   /* 空对象：什么都不改 */
        }

        for (;;)
        {
            char key[kMaxKeyLength];
            if (!parseKey(key, sizeof(key)))
            {
                return false;
            }

            skipSpaces();
            if (!consume(':'))
            {
                return false;
            }
            skipSpaces();

            int32_t value = 0;
            if (!parseInteger(&value))
            {
                return false;
            }

            if (!applyField(key, value, inOut))
            {
                return false;
            }

            skipSpaces();
            if (consume(','))
            {
                skipSpaces();
                continue;
            }
            if (consume('}'))
            {
                return true;
            }
            return false;
        }
    }

    private:
    bool consume(char expected)
    {
        if ((m_position < m_length) && (m_text[m_position] == expected))
        {
            m_position += 1;
            return true;
        }
        return false;
    }

    void skipSpaces()
    {
        while (m_position < m_length)
        {
            const char c = m_text[m_position];
            if ((c != ' ') && (c != '\t') && (c != '\r') && (c != '\n'))
            {
                break;
            }
            m_position += 1;
        }
    }

    bool parseKey(char *key, std::size_t capacity)
    {
        if (!consume('"'))
        {
            return false;
        }

        std::size_t count = 0;
        while (m_position < m_length)
        {
            const char c = m_text[m_position];
            m_position += 1;

            if (c == '"')
            {
                if (count == 0)
                {
                    return false;
                }
                key[count] = '\0';
                return true;
            }

            if ((c == '\\') || ((count + 1) >= capacity))
            {
                return false;   /* 不支持转义，键也不能超长 */
            }

            key[count] = c;
            count += 1;
        }

        return false;
    }

    bool parseInteger(int32_t *value)
    {
        bool negative = false;
        if (consume('-'))
        {
            negative = true;
        }

        if ((m_position >= m_length) || (m_text[m_position] < '0') || (m_text[m_position] > '9'))
        {
            return false;
        }

        int32_t magnitude = 0;
        while (m_position < m_length)
        {
            const char c = m_text[m_position];
            if ((c < '0') || (c > '9'))
            {
                break;
            }

            const int32_t digit = static_cast<int32_t>(c - '0');
            if (magnitude > ((2147483647 - digit) / 10))
            {
                return false;   /* 超出 int32 范围 */
            }

            magnitude = (magnitude * 10) + digit;
            m_position += 1;
        }

        *value = negative ? -magnitude : magnitude;
        return true;
    }

    bool applyField(const char *key, int32_t value, VariableStatics *config)
    {
        /* 温度/湿度：0.01 单位 */
        if (equals(key, "tMin"))
        {
            config->minTemperature = value / 100.0;
            return true;
        }
        if (equals(key, "tMax"))
        {
            config->maxTemperature = value / 100.0;
            return true;
        }
        if (equals(key, "tHys"))
        {
            config->temperatureHysteresis = value / 100.0;
            return true;
        }
        if (equals(key, "ahMin"))
        {
            config->minAirHumidity = value / 100.0;
            return true;
        }
        if (equals(key, "ahMax"))
        {
            config->maxAirHumidity = value / 100.0;
            return true;
        }
        if (equals(key, "ahHys"))
        {
            config->airHumidityHysteresis = value / 100.0;
            return true;
        }
        if (equals(key, "shMin"))
        {
            config->minSoilHumidity = value / 100.0;
            return true;
        }
        if (equals(key, "shMax"))
        {
            config->maxSoilHumidity = value / 100.0;
            return true;
        }

        /* 光照：lx */
        if (equals(key, "luxMin"))
        {
            config->minIlluminance = static_cast<double>(value);
            return true;
        }
        if (equals(key, "luxMax"))
        {
            config->maxIlluminance = static_cast<double>(value);
            return true;
        }

        /* 无符号毫秒量：负数直接拒绝 */
        if (equals(key, "luxOnMs"))
        {
            return assignUint32(value, &config->illuminanceMinOnMs);
        }
        if (equals(key, "dayStartMs"))
        {
            return assignUint32(value, &config->dayStartMs);
        }
        if (equals(key, "dayLenMs"))
        {
            return assignUint32(value, &config->dayLengthMs);
        }
        if (equals(key, "pumpMs"))
        {
            return assignUint32(value, &config->pumpDurationMs);
        }
        if (equals(key, "pumpIntervalMs"))
        {
            return assignUint32(value, &config->pumpMinIntervalMs);
        }
        if (equals(key, "pumpMaxPerDay"))
        {
            return assignUint32(value, &config->pumpMaxPerDay);
        }
        if (equals(key, "turnIntervalMs"))
        {
            return assignUint32(value, &config->turnIntervalMs);
        }
        if (equals(key, "sampleMs"))
        {
            return assignUint32(value, &config->samplePeriodMs);
        }

        /* 转盘：int16 范围 */
        if (equals(key, "turnMax"))
        {
            return assignInt16(value, &config->maxTurnAngle);
        }
        if (equals(key, "turnStep"))
        {
            return assignInt16(value, &config->turnStepAngle);
        }

        if (equals(key, "turnPerDay"))
        {
            config->turnAnglePerDay = static_cast<int>(value);
            return true;
        }

        /* 未知键：忽略，保持前向兼容 */
        return true;
    }

    static bool assignUint32(int32_t value, uint32_t *target)
    {
        if (value < 0)
        {
            return false;
        }
        *target = static_cast<uint32_t>(value);
        return true;
    }

    static bool assignInt16(int32_t value, int16_t *target)
    {
        if ((value < -32768) || (value > 32767))
        {
            return false;
        }
        *target = static_cast<int16_t>(value);
        return true;
    }

    const char *m_text;
    std::size_t m_length;
    std::size_t m_position;
};

} // namespace


bool ConfigurationPayload::decode(const char *payload, std::size_t length, VariableStatics *inOut)
{
    if ((payload == nullptr) || (inOut == nullptr))
    {
        return false;
    }

    Parser parser(payload, length);
    return parser.parseObject(inOut);
}

std::size_t ConfigurationPayload::buildAck(bool accepted,
                                           const char *reason,
                                           char *buffer,
                                           std::size_t capacity)
{
    TextWriter writer(buffer, capacity);

    bool ok = writer.append("{\"ok\":") && writer.append(accepted ? "true" : "false");

    if (!accepted)
    {
        ok = ok
             && writer.append(",\"reason\":\"")
             && writer.append((reason != nullptr) ? reason : "rejected")
             && writer.append('"');
    }

    ok = ok && writer.append('}');

    if (!ok || writer.isTruncated())
    {
        return 0;
    }

    return writer.length();
}
