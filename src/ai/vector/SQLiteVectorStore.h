#pragma once
#include "IVectorStore.h"
#include <sqlite3.h>
#include <mutex>

namespace tmms
{
    namespace ai
    {
        class SQLiteVectorStore : public IVectorStore
        {
        public:
            SQLiteVectorStore() = default;
            ~SQLiteVectorStore() override;

            bool Init(const std::string &db_path,
                      int dimension,
                      std::string &err) override;

            bool Insert(const InsertParams &params,
                        const std::vector<float> &embedding,
                        std::string &err) override;

            bool Search(const std::vector<float> &query_embedding,
                        const SearchParams &params,
                        std::vector<VectorSearchResult> &results,
                        std::string &err) override;

            bool DeleteByScope(int64_t user_id,
                               const std::string &scope_type,
                               int64_t scope_id,
                               std::string &err) override;

            bool Delete(const std::string &id, std::string &err) override;
            int Count() override;
            bool Clear(std::string &err) override;

        private:
            static float CosineSimilarity(const std::vector<float> &a,
                                          const std::vector<float> &b);
            static std::vector<uint8_t> FloatVecToBytes(const std::vector<float> &vec);
            static std::vector<float> BytesToFloatVec(const uint8_t *data, size_t size);

            sqlite3 *db_{nullptr};
            int dimension_{2048};
            std::mutex mutex_;
        };

    } // namespace ai
} // namespace tmms