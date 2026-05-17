#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace tmms
{
    namespace ai
    {
        struct VectorSearchResult
        {
            std::string id;
            std::string content;
            std::string source;
            std::string filename;
            std::string chunk_type;
            int start_line{0};
            int end_line{0};
            float score{0.0f};
        };

        class IVectorStore
        {
        public:
            virtual ~IVectorStore() = default;

            virtual bool Init(const std::string &db_path,
                              int dimension,
                              std::string &err) = 0;

            struct InsertParams
            {
                std::string id;
                int64_t user_id{0};

                // 统一范围：global / project / conversation
                std::string scope_type;
                int64_t scope_id{0}; // global=0, project=project_id, conversation=conversation_id

                std::string filename;
                std::string chunk_type{"text"}; // text / code
                int start_line{0};
                int end_line{0};

                std::string content;
                std::string content_hash;
                std::string source;
            };

            virtual bool Insert(const InsertParams &params,
                                const std::vector<float> &embedding,
                                std::string &err) = 0;

            struct SearchParams
            {
                int64_t user_id{0};
                std::string scope_type;
                int64_t scope_id{0};
                int top_k{3};
                float min_similarity{0.3f};
            };

            virtual bool Search(const std::vector<float> &query_embedding,
                                const SearchParams &params,
                                std::vector<VectorSearchResult> &results,
                                std::string &err) = 0;

            // 统一删除接口
            virtual bool DeleteByScope(int64_t user_id,
                                       const std::string &scope_type,
                                       int64_t scope_id,
                                       std::string &err) = 0;

            virtual bool Delete(const std::string &id, std::string &err) = 0;
            virtual int Count() = 0;
            virtual bool Clear(std::string &err) = 0;
        };

    } // namespace ai
} // namespace tmms