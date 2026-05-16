#pragma once
#include <string>
#include <vector>

namespace tmms
{
    namespace ai
    {
        struct VectorSearchResult
        {
            std::string id;
            std::string content;
            std::string source;
            float       score;
        };

        class IVectorStore
        {
        public:
            virtual ~IVectorStore() = default;

            virtual bool Init(const std::string &db_path,
                              int dimension,
                              std::string &err) = 0;

            virtual bool Insert(const std::string &id,
                                const std::string &content,
                                const std::vector<float> &embedding,
                                const std::string &source,
                                std::string &err) = 0;

            virtual bool Search(const std::vector<float> &query_embedding,
                                int top_k,
                                float min_similarity,
                                std::vector<VectorSearchResult> &results,
                                std::string &err) = 0;

            virtual bool Delete(const std::string &id,
                                std::string &err) = 0;

            virtual int Count() = 0;

            virtual bool Clear(std::string &err) = 0;
        };

    } // namespace ai
} // namespace tmms