#include "SQLiteVectorStore.h"
#include "base/LogStream.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <ctime>

namespace tmms
{
    namespace ai
    {
        SQLiteVectorStore::~SQLiteVectorStore()
        {
            if (db_)
            {
                sqlite3_close(db_);
                db_ = nullptr;
            }
        }

        bool SQLiteVectorStore::Init(const std::string &db_path,
                                     int dimension,
                                     std::string &err)
        {
            dimension_ = dimension;

            int rc = sqlite3_open(db_path.c_str(), &db_);
            if (rc != SQLITE_OK)
            {
                err = "sqlite3_open failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }

            const char *create_sql =
                "CREATE TABLE IF NOT EXISTS knowledge_chunks ("
                "  id         TEXT PRIMARY KEY,"
                "  content    TEXT NOT NULL,"
                "  embedding  BLOB NOT NULL,"
                "  source     TEXT DEFAULT '',"
                "  created_at INTEGER NOT NULL"
                ");";

            char *errmsg = nullptr;
            rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, &errmsg);
            if (rc != SQLITE_OK)
            {
                err = "create table failed: " + std::string(errmsg);
                sqlite3_free(errmsg);
                return false;
            }

            LOG_INFO << "SQLiteVectorStore init."
                     << " db=" << db_path
                     << " dimension=" << dimension_;
            return true;
        }

        bool SQLiteVectorStore::Insert(const std::string &id,
                                       const std::string &content,
                                       const std::vector<float> &embedding,
                                       const std::string &source,
                                       std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql =
                "INSERT OR REPLACE INTO knowledge_chunks"
                " (id, content, embedding, source, created_at)"
                " VALUES (?, ?, ?, ?, ?);";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare insert failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }

            auto bytes = FloatVecToBytes(embedding);
            int64_t ts = (int64_t)time(nullptr);

            sqlite3_bind_text(stmt, 1,
                id.c_str(), (int)id.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2,
                content.c_str(), (int)content.size(), SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 3,
                bytes.data(), (int)bytes.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4,
                source.c_str(), (int)source.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, ts);

            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE)
            {
                err = "insert failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }
            return true;
        }

        bool SQLiteVectorStore::Search(
            const std::vector<float> &query_embedding,
            int top_k,
            float min_similarity,
            std::vector<VectorSearchResult> &results,
            std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql =
                "SELECT id, content, embedding, source"
                " FROM knowledge_chunks;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare select failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }

            std::vector<VectorSearchResult> all;

            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                std::string id =
                    reinterpret_cast<const char *>(
                        sqlite3_column_text(stmt, 0));
                std::string content =
                    reinterpret_cast<const char *>(
                        sqlite3_column_text(stmt, 1));

                const uint8_t *blob =
                    static_cast<const uint8_t *>(
                        sqlite3_column_blob(stmt, 2));
                int blob_size = sqlite3_column_bytes(stmt, 2);

                std::string source;
                const unsigned char *src_ptr =
                    sqlite3_column_text(stmt, 3);
                if (src_ptr)
                    source = reinterpret_cast<const char *>(src_ptr);

                auto emb = BytesToFloatVec(blob, (size_t)blob_size);
                float score = CosineSimilarity(query_embedding, emb);

                if (score >= min_similarity)
                {
                    VectorSearchResult r;
                    r.id      = id;
                    r.content = content;
                    r.source  = source;
                    r.score   = score;
                    all.push_back(std::move(r));
                }
            }
            sqlite3_finalize(stmt);

            std::sort(all.begin(), all.end(),
                [](const VectorSearchResult &a,
                   const VectorSearchResult &b)
                {
                    return a.score > b.score;
                });

            int n = std::min((int)all.size(), top_k);
            results.assign(all.begin(), all.begin() + n);

            return true;
        }

        bool SQLiteVectorStore::Delete(const std::string &id,
                                       std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql =
                "DELETE FROM knowledge_chunks WHERE id = ?;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare delete failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }

            sqlite3_bind_text(stmt, 1,
                id.c_str(), (int)id.size(), SQLITE_TRANSIENT);

            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE)
            {
                err = "delete failed: "
                      + std::string(sqlite3_errmsg(db_));
                return false;
            }
            return true;
        }

        int SQLiteVectorStore::Count()
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql =
                "SELECT COUNT(*) FROM knowledge_chunks;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
                return -1;

            int count = 0;
            if (sqlite3_step(stmt) == SQLITE_ROW)
                count = sqlite3_column_int(stmt, 0);

            sqlite3_finalize(stmt);
            return count;
        }

        bool SQLiteVectorStore::Clear(std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            char *errmsg = nullptr;
            int rc = sqlite3_exec(db_,
                "DELETE FROM knowledge_chunks;",
                nullptr, nullptr, &errmsg);

            if (rc != SQLITE_OK)
            {
                err = "clear failed: " + std::string(errmsg);
                sqlite3_free(errmsg);
                return false;
            }
            return true;
        }

        float SQLiteVectorStore::CosineSimilarity(
            const std::vector<float> &a,
            const std::vector<float> &b)
        {
            if (a.size() != b.size() || a.empty())
                return 0.0f;

            double dot    = 0.0;
            double norm_a = 0.0;
            double norm_b = 0.0;

            for (size_t i = 0; i < a.size(); ++i)
            {
                dot    += (double)a[i] * (double)b[i];
                norm_a += (double)a[i] * (double)a[i];
                norm_b += (double)b[i] * (double)b[i];
            }

            if (norm_a < 1e-10 || norm_b < 1e-10)
                return 0.0f;

            return (float)(dot / (std::sqrt(norm_a) * std::sqrt(norm_b)));
        }

        std::vector<uint8_t> SQLiteVectorStore::FloatVecToBytes(
            const std::vector<float> &vec)
        {
            std::vector<uint8_t> bytes(vec.size() * sizeof(float));
            std::memcpy(bytes.data(), vec.data(), bytes.size());
            return bytes;
        }

        std::vector<float> SQLiteVectorStore::BytesToFloatVec(
            const uint8_t *data, size_t size)
        {
            size_t n = size / sizeof(float);
            std::vector<float> vec(n);
            std::memcpy(vec.data(), data, size);
            return vec;
        }

    } // namespace ai
} // namespace tmms