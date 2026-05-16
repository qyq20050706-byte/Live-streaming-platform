#include "MessageRepo.h"
#include "base/LogStream.h"
#include <mysql/mysql.h>
#include <cstring>
#include <mutex>
#include <algorithm>

namespace tmms
{
    namespace ai
    {
        MessageRepo::MessageRepo(MySQLClient *client)
            : db_(client)
        {
        }

        uint64_t MessageRepo::Insert(uint64_t conversation_id,
                                     const std::string &role,
                                     const std::string &content,
                                     std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "INSERT INTO messages (conversation_id, role, content) "
                "VALUES (?, ?, ?)";

            MYSQL_STMT *stmt = mysql_stmt_init(conn);
            if (!stmt)
            {
                err = "mysql_stmt_init failed";
                return 0;
            }

            if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return 0;
            }

            MYSQL_BIND bind[3];
            memset(bind, 0, sizeof(bind));

            unsigned long role_len = role.size();
            unsigned long content_len = content.size();

            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &conversation_id;
            bind[0].is_unsigned = true;

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (void *)role.c_str();
            bind[1].buffer_length = role.size();
            bind[1].length = &role_len;

            bind[2].buffer_type = MYSQL_TYPE_STRING;
            bind[2].buffer = (void *)content.c_str();
            bind[2].buffer_length = content.size();
            bind[2].length = &content_len;

            if (mysql_stmt_bind_param(stmt, bind) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return 0;
            }

            if (mysql_stmt_execute(stmt) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return 0;
            }

            uint64_t new_id = mysql_stmt_insert_id(stmt);
            mysql_stmt_close(stmt);
            return new_id;
        }

        bool MessageRepo::ListRecent(uint64_t conversation_id,
                                     int limit,
                                     std::vector<MessageRecord> &out,
                                     std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            // 先取最近 N 条（DESC），再反转成正序供 LLM 使用
            MYSQL *conn = db_->Raw();
            const char *sql =
                "SELECT id, conversation_id, role, content, "
                "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') "
                "FROM messages "
                "WHERE conversation_id = ? "
                "ORDER BY created_at DESC "
                "LIMIT ?";

            MYSQL_STMT *stmt = mysql_stmt_init(conn);
            if (!stmt)
            {
                err = "mysql_stmt_init failed";
                return false;
            }

            if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            MYSQL_BIND param[2];
            memset(param, 0, sizeof(param));
            int limit_val = limit;

            param[0].buffer_type = MYSQL_TYPE_LONGLONG;
            param[0].buffer = &conversation_id;
            param[0].is_unsigned = true;

            param[1].buffer_type = MYSQL_TYPE_LONG;
            param[1].buffer = &limit_val;

            if (mysql_stmt_bind_param(stmt, param) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            if (mysql_stmt_execute(stmt) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            uint64_t id{0}, conv_id{0};
            // content 用 MEDIUMTEXT，最大 16MB，这里用动态方式处理
            char role_buf[17] = {};
            char created_buf[32] = {};
            unsigned long id_len, conv_len, role_len, content_len, cr_len;
            bool is_null[5] = {}, err_flags[5] = {};

            // content 先用固定缓冲区，超长时用 mysql_stmt_fetch_column
            // 对于普通对话内容，4096 字节足够；超长内容通过 fetch_column 补读
            const size_t CONTENT_BUF_SIZE = 4096;
            std::vector<char> content_buf(CONTENT_BUF_SIZE, 0);

            MYSQL_BIND result[5];
            memset(result, 0, sizeof(result));

            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &id;
            result[0].is_unsigned = true;
            result[0].length = &id_len;
            result[0].is_null = &is_null[0];
            result[0].error = &err_flags[0];

            result[1].buffer_type = MYSQL_TYPE_LONGLONG;
            result[1].buffer = &conv_id;
            result[1].is_unsigned = true;
            result[1].length = &conv_len;
            result[1].is_null = &is_null[1];
            result[1].error = &err_flags[1];

            result[2].buffer_type = MYSQL_TYPE_STRING;
            result[2].buffer = role_buf;
            result[2].buffer_length = sizeof(role_buf) - 1;
            result[2].length = &role_len;
            result[2].is_null = &is_null[2];
            result[2].error = &err_flags[2];

            result[3].buffer_type = MYSQL_TYPE_STRING;
            result[3].buffer = content_buf.data();
            result[3].buffer_length = CONTENT_BUF_SIZE - 1;
            result[3].length = &content_len;
            result[3].is_null = &is_null[3];
            result[3].error = &err_flags[3];

            result[4].buffer_type = MYSQL_TYPE_STRING;
            result[4].buffer = created_buf;
            result[4].buffer_length = sizeof(created_buf) - 1;
            result[4].length = &cr_len;
            result[4].is_null = &is_null[4];
            result[4].error = &err_flags[4];

            if (mysql_stmt_bind_result(stmt, result) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            out.clear();
            while (true)
            {
                // 重置缓冲区
                memset(content_buf.data(), 0, CONTENT_BUF_SIZE);

                int ret = mysql_stmt_fetch(stmt);
                if (ret == MYSQL_NO_DATA)
                    break;
                if (ret != 0 && ret != MYSQL_DATA_TRUNCATED)
                    break;

                MessageRecord rec;
                rec.id = id;
                rec.conversation_id = conv_id;
                rec.role = std::string(role_buf, role_len);
                rec.created_at = std::string(created_buf, cr_len);

                if (ret == MYSQL_DATA_TRUNCATED && err_flags[3])
                {
                    // content 被截断，重新分配并用 fetch_column 补读
                    std::vector<char> big_buf(content_len + 1, 0);
                    MYSQL_BIND col_bind;
                    memset(&col_bind, 0, sizeof(col_bind));
                    col_bind.buffer_type = MYSQL_TYPE_STRING;
                    col_bind.buffer = big_buf.data();
                    col_bind.buffer_length = content_len;
                    col_bind.length = &content_len;

                    mysql_stmt_fetch_column(stmt, &col_bind, 3, 0);
                    rec.content = std::string(big_buf.data(), content_len);
                }
                else
                {
                    rec.content = std::string(content_buf.data(),
                                              std::min(content_len,
                                                       (unsigned long)CONTENT_BUF_SIZE - 1));
                }

                out.push_back(std::move(rec));
            }

            mysql_stmt_close(stmt);

            // 反转：数据库取的是 DESC（最新在前），反转后变成正序（旧→新）
            std::reverse(out.begin(), out.end());
            return true;
        }

        int MessageRepo::Count(uint64_t conversation_id, std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "SELECT COUNT(*) FROM messages WHERE conversation_id = ?";

            MYSQL_STMT *stmt = mysql_stmt_init(conn);
            if (!stmt)
            {
                err = "mysql_stmt_init failed";
                return -1;
            }

            if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return -1;
            }

            MYSQL_BIND param[1];
            memset(param, 0, sizeof(param));
            param[0].buffer_type = MYSQL_TYPE_LONGLONG;
            param[0].buffer = &conversation_id;
            param[0].is_unsigned = true;

            if (mysql_stmt_bind_param(stmt, param) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return -1;
            }

            if (mysql_stmt_execute(stmt) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return -1;
            }

            int64_t count = 0;
            unsigned long count_len;
            bool is_null = false, err_flag = false;

            MYSQL_BIND result[1];
            memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &count;
            result[0].length = &count_len;
            result[0].is_null = &is_null;
            result[0].error = &err_flag;

            mysql_stmt_bind_result(stmt, result);
            mysql_stmt_fetch(stmt);
            mysql_stmt_close(stmt);

            return (int)count;
        }

    } // namespace ai
} // namespace tmms