#pragma once
#include "IEmbeddingProvider.h"
#include "ai/http/HttpClient.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace tmms
{
    namespace ai
    {
        class DoubaoEmbedding : public IEmbeddingProvider
        {
        public:
            DoubaoEmbedding()  = default;
            ~DoubaoEmbedding() override = default;

            bool Init(const std::string &config_file, std::string &err);

            bool Embed(const std::string &text,
                       std::vector<float> &embedding,
                       std::string &err) override;

            int Dimension() const override { return dimension_; }

        private:
            std::string base_url_;
            std::string api_key_;
            std::string model_;
            int         timeout_ms_{30000};
            int         dimension_{3072};
            HttpClient  client_;
        };

    } // namespace ai
} // namespace tmms