#pragma once
#include "TextSplitter.h"
#include "PromptTemplate.h"
#include "ai/embedding/IEmbeddingProvider.h"
#include "ai/vector/IVectorStore.h"
#include "ai/llm/ILLMProvider.h"
#include <functional>
#include <string>
#include <vector>
#include <atomic>

namespace tmms
{
    namespace ai
    {
        struct RAGConfig
        {
            std::string db_path = "bin/data/rag.db";
            int chunk_size = 300;
            int chunk_overlap = 50;
            int top_k = 3;
            float min_similarity = 0.3f;
        };

        class RAGService
        {
        public:
            RAGService(IEmbeddingProvider *embedder,
                       IVectorStore *store,
                       ILLMProvider *llm);

            int AddKnowledge(const std::string &text,
                             const std::string &source,
                             std::string &err);

            bool Chat(const std::string &query,
                      std::string &answer,
                      std::string &err);

            bool ChatStream(
                const std::string &query,
                std::function<void(const std::string &)> callback,
                std::string &err);

        public:
            int Count();

            bool Clear(std::string &err);

            void SetConfig(const RAGConfig &cfg);

        private:
            bool Retrieve(const std::string &query,
                          std::vector<std::string> &contexts,
                          std::string &err);

            IEmbeddingProvider *embedder_{nullptr};
            IVectorStore *store_{nullptr};
            ILLMProvider *llm_{nullptr};

            TextSplitter splitter_;
            RAGConfig config_;
            std::atomic<uint64_t> chunk_counter_{0};
        };

    } // namespace ai
} // namespace tmms