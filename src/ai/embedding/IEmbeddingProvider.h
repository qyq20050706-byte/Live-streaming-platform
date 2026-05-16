#pragma once
#include <string>
#include <vector>

namespace tmms
{
    namespace ai
    {
        class IEmbeddingProvider
        {
        public:
            virtual ~IEmbeddingProvider() = default;

            virtual bool Embed(const std::string &text,
                               std::vector<float> &embedding,
                               std::string &err) = 0;

            virtual int Dimension() const = 0;
        };

    } // namespace ai
} // namespace tmms