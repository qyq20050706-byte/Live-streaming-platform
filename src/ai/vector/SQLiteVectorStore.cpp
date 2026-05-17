#include "SQLiteVectorStore.h"
#include "ai/RagScope.h"
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
                err = "sqlite3_open failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            // 避免并发写时立即报 database is locked
            sqlite3_busy_timeout(db_, 5000);

            // 提升并发读写能力
            sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "PRAGMA cache_size=-10000;", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

            // 如果表不存在则建表（与你已经建好的结构一致）
            const char *create_sql = R"(
CREATE TABLE IF NOT EXISTS knowledge_chunks (
    id           TEXT    NOT NULL PRIMARY KEY,
    user_id      INTEGER NOT NULL,
    scope_type   TEXT    NOT NULL,
    scope_id     INTEGER NOT NULL DEFAULT 0,
    filename     TEXT    NOT NULL DEFAULT '',
    chunk_type   TEXT    NOT NULL DEFAULT 'text',
    start_line   INTEGER NOT NULL DEFAULT 0,
    end_line     INTEGER NOT NULL DEFAULT 0,
    content      TEXT    NOT NULL,
    content_hash TEXT    NOT NULL DEFAULT '',
    embedding    BLOB    NOT NULL,
    source       TEXT    NOT NULL DEFAULT '',
    created_at   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_kc_user_scope
    ON knowledge_chunks(user_id, scope_type, scope_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_kc_user_scope_hash
    ON knowledge_chunks(user_id, scope_type, scope_id, content_hash)
    WHERE content_hash != '';
)";

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

        bool SQLiteVectorStore::Insert(const InsertParams &params,
                                       const std::vector<float> &embedding,
                                       std::string &err)
        {
            // 参数校验
            if (params.user_id <= 0)
            {
                err = "invalid user_id: " + std::to_string(params.user_id);
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if (!IsValidScopeType(params.scope_type))
            {
                err = "invalid scope_type: " + params.scope_type;
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if (params.scope_type == SCOPE_GLOBAL && params.scope_id != 0)
            {
                err = "global scope must use scope_id = 0";
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if ((params.scope_type == SCOPE_PROJECT ||
                 params.scope_type == SCOPE_CONVERSATION) &&
                params.scope_id <= 0)
            {
                err = "non-global scope must use positive scope_id";
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if (params.id.empty())
            {
                err = "id cannot be empty";
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if (params.content.empty())
            {
                err = "content cannot be empty";
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }
            if ((int)embedding.size() != dimension_)
            {
                err = "embedding dimension mismatch: expected " + std::to_string(dimension_) + ", got " + std::to_string(embedding.size());
                LOG_ERROR << "SQLiteVectorStore::Insert: " << err;
                return false;
            }

            std::lock_guard<std::mutex> lk(mutex_);

            // content_hash 去重（同 user + scope 下）
            if (!params.content_hash.empty())
            {
                const char *check_sql =
                    "SELECT COUNT(*) FROM knowledge_chunks"
                    " WHERE user_id=? AND scope_type=? AND scope_id=? AND content_hash=?;";
                sqlite3_stmt *check_stmt = nullptr;
                if (sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int64(check_stmt, 1, params.user_id);
                    sqlite3_bind_text(check_stmt, 2, params.scope_type.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(check_stmt, 3, params.scope_id);
                    sqlite3_bind_text(check_stmt, 4, params.content_hash.c_str(), -1, SQLITE_TRANSIENT);

                    bool is_dup = (sqlite3_step(check_stmt) == SQLITE_ROW &&
                                   sqlite3_column_int(check_stmt, 0) > 0);
                    sqlite3_finalize(check_stmt);

                    if (is_dup)
                    {
                        LOG_INFO << "SQLiteVectorStore::Insert: skip duplicate"
                                 << " user_id=" << params.user_id
                                 << " scope=" << params.scope_type
                                 << " scope_id=" << params.scope_id;
                        return true;
                    }
                }
            }

            const char *sql =
                "INSERT OR IGNORE INTO knowledge_chunks"
                " (id, user_id, scope_type, scope_id,"
                "  filename, chunk_type, start_line, end_line,"
                "  content, content_hash, embedding, source, created_at)"
                " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?);";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare insert failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            auto bytes = FloatVecToBytes(embedding);
            int64_t ts = (int64_t)time(nullptr);

            sqlite3_bind_text(stmt, 1, params.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, params.user_id);
            sqlite3_bind_text(stmt, 3, params.scope_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, params.scope_id);
            sqlite3_bind_text(stmt, 5, params.filename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, params.chunk_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, params.start_line);
            sqlite3_bind_int(stmt, 8, params.end_line);
            sqlite3_bind_text(stmt, 9, params.content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, params.content_hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 11, bytes.data(), (int)bytes.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 12, params.source.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 13, ts);

            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE)
            {
                err = "insert failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            LOG_DEBUG << "SQLiteVectorStore::Insert success"
                      << " user_id=" << params.user_id
                      << " scope=" << params.scope_type
                      << " scope_id=" << params.scope_id
                      << " filename=" << params.filename
                      << " lines=" << params.start_line << "-" << params.end_line;
            return true;
        }

        bool SQLiteVectorStore::Search(const std::vector<float> &query_embedding,
                                       const SearchParams &params,
                                       std::vector<VectorSearchResult> &results,
                                       std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);
            results.clear();

            if (params.user_id <= 0)
            {
                err = "invalid user_id";
                return false;
            }
            if (!IsValidScopeType(params.scope_type))
            {
                err = "invalid scope_type: " + params.scope_type;
                return false;
            }

            const char *sql =
                "SELECT id, content, source, filename, chunk_type, start_line, end_line, embedding"
                " FROM knowledge_chunks"
                " WHERE user_id=? AND scope_type=? AND scope_id=?;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare search failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            sqlite3_bind_int64(stmt, 1, params.user_id);
            sqlite3_bind_text(stmt, 2, params.scope_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, params.scope_id);

            std::vector<VectorSearchResult> tmp;
            tmp.reserve(params.top_k * 2);

            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                std::string id =
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                std::string content =
                    reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));

                std::string source, filename, chunk_type;
                const unsigned char *p;
                if ((p = sqlite3_column_text(stmt, 2)))
                    source = reinterpret_cast<const char *>(p);
                if ((p = sqlite3_column_text(stmt, 3)))
                    filename = reinterpret_cast<const char *>(p);
                if ((p = sqlite3_column_text(stmt, 4)))
                    chunk_type = reinterpret_cast<const char *>(p);

                int start_line = sqlite3_column_int(stmt, 5);
                int end_line = sqlite3_column_int(stmt, 6);

                const uint8_t *blob = static_cast<const uint8_t *>(
                    sqlite3_column_blob(stmt, 7));
                int blob_size = sqlite3_column_bytes(stmt, 7);
                auto emb = BytesToFloatVec(blob, blob_size);

                float score = CosineSimilarity(query_embedding, emb);
                if (score >= params.min_similarity)
                {
                    VectorSearchResult r;
                    r.id = id;
                    r.content = content;
                    r.source = source;
                    r.filename = filename;
                    r.chunk_type = chunk_type;
                    r.start_line = start_line;
                    r.end_line = end_line;
                    r.score = score;
                    tmp.push_back(std::move(r));
                }
            }
            sqlite3_finalize(stmt);

            std::sort(tmp.begin(), tmp.end(),
                      [](const VectorSearchResult &a, const VectorSearchResult &b)
                      { return a.score > b.score; });

            int n = std::min((int)tmp.size(), params.top_k);
            results.assign(tmp.begin(), tmp.begin() + n);

            LOG_INFO << "SQLiteVectorStore::Search"
                     << " user_id=" << params.user_id
                     << " scope=" << params.scope_type
                     << " scope_id=" << params.scope_id
                     << " hits=" << results.size();

            return true;
        }

        bool SQLiteVectorStore::DeleteByScope(int64_t user_id,
                                              const std::string &scope_type,
                                              int64_t scope_id,
                                              std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql =
                "DELETE FROM knowledge_chunks"
                " WHERE user_id=? AND scope_type=? AND scope_id=?;";

            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare delete failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            sqlite3_bind_int64(stmt, 1, user_id);
            sqlite3_bind_text(stmt, 2, scope_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, scope_id);

            int rc = sqlite3_step(stmt);
            int changed = sqlite3_changes(db_);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE)
            {
                err = "delete failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            LOG_INFO << "SQLiteVectorStore::DeleteByScope"
                     << " user_id=" << user_id
                     << " scope=" << scope_type
                     << " scope_id=" << scope_id
                     << " deleted=" << changed;
            return true;
        }

        bool SQLiteVectorStore::Delete(const std::string &id, std::string &err)
        {
            std::lock_guard<std::mutex> lk(mutex_);

            const char *sql = "DELETE FROM knowledge_chunks WHERE id=?;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                err = "prepare delete failed: " + std::string(sqlite3_errmsg(db_));
                return false;
            }

            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE)
            {
                err = "delete failed: " + std::string(sqlite3_errmsg(db_));
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
            int rc = sqlite3_exec(db_, "DELETE FROM knowledge_chunks;",
                                  nullptr, nullptr, &errmsg);
            if (rc != SQLITE_OK)
            {
                err = "clear failed: " + std::string(errmsg);
                sqlite3_free(errmsg);
                return false;
            }
            return true;
        }

        float SQLiteVectorStore::CosineSimilarity(const std::vector<float> &a,
                                                  const std::vector<float> &b)
        {
            if (a.size() != b.size() || a.empty())
                return 0.0f;
            double dot = 0.0, na = 0.0, nb = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                dot += (double)a[i] * b[i];
                na += (double)a[i] * a[i];
                nb += (double)b[i] * b[i];
            }
            if (na < 1e-10 || nb < 1e-10)
                return 0.0f;
            return (float)(dot / (std::sqrt(na) * std::sqrt(nb)));
        }

        std::vector<uint8_t> SQLiteVectorStore::FloatVecToBytes(const std::vector<float> &vec)
        {
            std::vector<uint8_t> bytes(vec.size() * sizeof(float));
            std::memcpy(bytes.data(), vec.data(), bytes.size());
            return bytes;
        }

        std::vector<float> SQLiteVectorStore::BytesToFloatVec(const uint8_t *data, size_t size)
        {
            size_t n = size / sizeof(float);
            std::vector<float> vec(n);
            std::memcpy(vec.data(), data, size);
            return vec;
        }

    } // namespace ai
} // namespace tmms