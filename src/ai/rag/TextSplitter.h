#pragma once
#include <string>
#include <vector>

namespace tmms
{
    namespace ai
    {
        class TextSplitter
        {
        public:
            TextSplitter(int chunk_size = 300, int overlap = 50);

            std::vector<std::string> Split(const std::string &text) const;

        private:
            int chunk_size_;
            int overlap_;
        };

    } // namespace ai
} // namespace tmms