//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_TEXTWRITER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_TEXTWRITER_H

#include <cstddef>
#include <cstdint>


/**
 * 定长缓冲区上的文本写入器。
 *
 * 不分配内存，所有写入都检查容量。容量不足时整次写入被拒绝并置截断标记，
 * 保证缓冲区里不会留下"半截"内容；缓冲区始终以 '\0' 结尾。
 */
class TextWriter
{
    public:
    TextWriter(char *buffer, std::size_t capacity);

    /* 清空内容，可重复使用 */
    void reset();

    bool append(const char *text);
    bool append(char character);
    bool appendInt(int32_t value);
    bool appendUnsigned(uint32_t value);
    bool appendBool(bool value);

    const char *data() const;
    std::size_t length() const;
    std::size_t capacity() const;
    bool isTruncated() const;

    private:
    /* 检查剩余空间是否还能放下 count 个字符（含结尾 '\0'） */
    bool reserve(std::size_t count);

    char *m_buffer;
    std::size_t m_capacity;
    std::size_t m_length;
    bool m_truncated;
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_TEXTWRITER_H
