//
// Created by APairOfMoons on 2026/11/3.
//

#include "TextWriter.h"

#include <cstring>


TextWriter::TextWriter(char *buffer, std::size_t capacity)
    : m_buffer(buffer), m_capacity(capacity), m_length(0), m_truncated(false)
{
    if ((m_buffer != nullptr) && (m_capacity > 0))
    {
        m_buffer[0] = '\0';
    }
}

void TextWriter::reset()
{
    m_length = 0;
    m_truncated = false;

    if ((m_buffer != nullptr) && (m_capacity > 0))
    {
        m_buffer[0] = '\0';
    }
}

bool TextWriter::reserve(std::size_t count)
{
    if ((m_buffer == nullptr) || (m_capacity == 0))
    {
        m_truncated = true;
        return false;
    }

    /* 需要 m_length + count + 1 字节；用减法避免加法溢出 */
    if (count >= (m_capacity - m_length))
    {
        m_truncated = true;
        return false;
    }

    return true;
}

bool TextWriter::append(const char *text)
{
    if (text == nullptr)
    {
        return false;
    }

    const std::size_t textLength = std::strlen(text);
    if (!reserve(textLength))
    {
        return false;
    }

    std::memcpy(m_buffer + m_length, text, textLength);
    m_length += textLength;
    m_buffer[m_length] = '\0';

    return true;
}

bool TextWriter::append(char character)
{
    if (!reserve(1))
    {
        return false;
    }

    m_buffer[m_length] = character;
    m_length += 1;
    m_buffer[m_length] = '\0';

    return true;
}

bool TextWriter::appendUnsigned(uint32_t value)
{
    char digits[10];   /* uint32_t 最多 10 位十进制数字 */
    std::size_t count = 0;

    do
    {
        digits[count] = static_cast<char>('0' + (value % 10U));
        count += 1;
        value /= 10U;
    } while (value != 0U);

    if (!reserve(count))
    {
        return false;
    }

    for (std::size_t i = 0; i < count; i++)
    {
        m_buffer[m_length] = digits[count - 1 - i];
        m_length += 1;
    }
    m_buffer[m_length] = '\0';

    return true;
}

bool TextWriter::appendInt(int32_t value)
{
    if (value < 0)
    {
        if (!append('-'))
        {
            return false;
        }

        /* 先加 1 再取负，避免 INT32_MIN 直接取负溢出 */
        const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1U;
        return appendUnsigned(magnitude);
    }

    return appendUnsigned(static_cast<uint32_t>(value));
}

bool TextWriter::appendBool(bool value)
{
    return append(value ? '1' : '0');
}

const char *TextWriter::data() const
{
    return m_buffer;
}

std::size_t TextWriter::length() const
{
    return m_length;
}

std::size_t TextWriter::capacity() const
{
    return m_capacity;
}

bool TextWriter::isTruncated() const
{
    return m_truncated;
}
