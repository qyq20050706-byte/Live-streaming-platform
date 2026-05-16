#include "TextSplitter.h"
#include <algorithm>

namespace tmms
{
    namespace ai
    {
        TextSplitter::TextSplitter(int chunk_size, int overlap)
            : chunk_size_(chunk_size), overlap_(overlap)
        {
        }

        std::vector<std::string> TextSplitter::Split(
            const std::string &text) const
        {
            std::vector<std::string> chunks;

            if (text.empty())
                return chunks;

            int len = (int)text.size();

            if (len <= chunk_size_)
            {
                chunks.push_back(text);
                return chunks;
            }

            int start = 0;
            while (start < len)
            {
                int end = std::min(start + chunk_size_, len);

                // 不切断 UTF-8 多字节字符（后续字节以 10xxxxxx 开头）
                while (end < len && (text[end] & 0xC0) == 0x80)
                    ++end;

                chunks.push_back(text.substr(start, end - start));

                int next = start + chunk_size_ - overlap_;

                while (next < len && (text[next] & 0xC0) == 0x80)
                    ++next;

                if (next <= start)
                    next = start + 1;

                start = next;
            }

            return chunks;
        }

    } // namespace ai
} // namespace tmms