#include "ConversationRepo.h"
#include "base/LogStream.h"
#include <mysql/mysql.h>
#include <cstring>
#include <mutex>

namespace tmms
{
    namespace ai
    {
        ConversationRepo::ConversationRepo(MySQLClient *client)
            : db_(client)
        {
        }

        uint64_t ConversationRepo::Create(uint64_t user_id,
                                          const std::string &title,
                                          const std::string &mode,
                                          std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "INSERT INTO conversations (user_id, title, mode, status) "
                "VALUES (?, ?, ?, 1)";

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

            unsigned long title_len = title.size();
            unsigned long mode_len = mode.size();

            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &user_id;
            bind[0].is_unsigned = true;

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (void *)title.c_str();
            bind[1].buffer_length = title.size();
            bind[1].length = &title_len;

            bind[2].buffer_type = MYSQL_TYPE_STRING;
            bind[2].buffer = (void *)mode.c_str();
            bind[2].buffer_length = mode.size();
            bind[2].length = &mode_len;

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

            LOG_INFO << "ConversationRepo::Create id=" << new_id
                     << " user_id=" << user_id;
            return new_id;
        }

        bool ConversationRepo::ListByUser(uint64_t user_id,
                                          int limit,
                                          std::vector<ConversationRecord> &out,
                                          std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "SELECT id, user_id, title, mode, status, "
                "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s'), "
                "DATE_FORMAT(updated_at,'%Y-%m-%d %H:%i:%s') "
                "FROM conversations "
                "WHERE user_id = ? AND status = 1 "
                "ORDER BY updated_at DESC "
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
            param[0].buffer = &user_id;
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

            uint64_t id{0}, uid{0};
            char title_buf[256] = {};
            char mode_buf[33] = {};
            char created_buf[32] = {};
            char updated_buf[32] = {};
            int status{0};
            unsigned long id_len, uid_len, title_len, mode_len,
                st_len, cr_len, up_len;
            bool is_null[7] = {}, err_flags[7] = {};

            MYSQL_BIND result[7];
            memset(result, 0, sizeof(result));

            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &id;
            result[0].is_unsigned = true;
            result[0].length = &id_len;
            result[0].is_null = &is_null[0];
            result[0].error = &err_flags[0];

            result[1].buffer_type = MYSQL_TYPE_LONGLONG;
            result[1].buffer = &uid;
            result[1].is_unsigned = true;
            result[1].length = &uid_len;
            result[1].is_null = &is_null[1];
            result[1].error = &err_flags[1];

            result[2].buffer_type = MYSQL_TYPE_STRING;
            result[2].buffer = title_buf;
            result[2].buffer_length = sizeof(title_buf) - 1;
            result[2].length = &title_len;
            result[2].is_null = &is_null[2];
            result[2].error = &err_flags[2];

            result[3].buffer_type = MYSQL_TYPE_STRING;
            result[3].buffer = mode_buf;
            result[3].buffer_length = sizeof(mode_buf) - 1;
            result[3].length = &mode_len;
            result[3].is_null = &is_null[3];
            result[3].error = &err_flags[3];

            result[4].buffer_type = MYSQL_TYPE_TINY;
            result[4].buffer = &status;
            result[4].length = &st_len;
            result[4].is_null = &is_null[4];
            result[4].error = &err_flags[4];

            result[5].buffer_type = MYSQL_TYPE_STRING;
            result[5].buffer = created_buf;
            result[5].buffer_length = sizeof(created_buf) - 1;
            result[5].length = &cr_len;
            result[5].is_null = &is_null[5];
            result[5].error = &err_flags[5];

            result[6].buffer_type = MYSQL_TYPE_STRING;
            result[6].buffer = updated_buf;
            result[6].buffer_length = sizeof(updated_buf) - 1;
            result[6].length = &up_len;
            result[6].is_null = &is_null[6];
            result[6].error = &err_flags[6];

            if (mysql_stmt_bind_result(stmt, result) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            out.clear();
            while (true)
            {
                int ret = mysql_stmt_fetch(stmt);
                if (ret == MYSQL_NO_DATA)
                    break;
                if (ret != 0)
                    break;

                ConversationRecord rec;
                rec.id = id;
                rec.user_id = uid;
                rec.title = std::string(title_buf, title_len);
                rec.mode = std::string(mode_buf, mode_len);
                rec.status = status;
                rec.created_at = std::string(created_buf, cr_len);
                rec.updated_at = std::string(updated_buf, up_len);
                out.push_back(std::move(rec));
            }

            mysql_stmt_close(stmt);
            return true;
        }

        bool ConversationRepo::FindById(uint64_t conv_id,
                                        ConversationRecord &out,
                                        std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "SELECT id, user_id, title, mode, status "
                "FROM conversations WHERE id = ? LIMIT 1";

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

            MYSQL_BIND param[1];
            memset(param, 0, sizeof(param));
            param[0].buffer_type = MYSQL_TYPE_LONGLONG;
            param[0].buffer = &conv_id;
            param[0].is_unsigned = true;

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

            uint64_t id{0}, uid{0};
            char title_buf[256] = {};
            char mode_buf[33] = {};
            int status{0};
            unsigned long id_len, uid_len, title_len, mode_len, st_len;
            bool is_null[5] = {}, err_flags[5] = {};

            MYSQL_BIND result[5];
            memset(result, 0, sizeof(result));

            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &id;
            result[0].is_unsigned = true;
            result[0].length = &id_len;
            result[0].is_null = &is_null[0];
            result[0].error = &err_flags[0];

            result[1].buffer_type = MYSQL_TYPE_LONGLONG;
            result[1].buffer = &uid;
            result[1].is_unsigned = true;
            result[1].length = &uid_len;
            result[1].is_null = &is_null[1];
            result[1].error = &err_flags[1];

            result[2].buffer_type = MYSQL_TYPE_STRING;
            result[2].buffer = title_buf;
            result[2].buffer_length = sizeof(title_buf) - 1;
            result[2].length = &title_len;
            result[2].is_null = &is_null[2];
            result[2].error = &err_flags[2];

            result[3].buffer_type = MYSQL_TYPE_STRING;
            result[3].buffer = mode_buf;
            result[3].buffer_length = sizeof(mode_buf) - 1;
            result[3].length = &mode_len;
            result[3].is_null = &is_null[3];
            result[3].error = &err_flags[3];

            result[4].buffer_type = MYSQL_TYPE_TINY;
            result[4].buffer = &status;
            result[4].length = &st_len;
            result[4].is_null = &is_null[4];
            result[4].error = &err_flags[4];

            if (mysql_stmt_bind_result(stmt, result) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            int fetch_ret = mysql_stmt_fetch(stmt);
            mysql_stmt_close(stmt);

            if (fetch_ret == MYSQL_NO_DATA)
            {
                err = "conversation not found";
                return false;
            }
            if (fetch_ret != 0)
            {
                err = "fetch failed";
                return false;
            }

            out.id = id;
            out.user_id = uid;
            out.title = std::string(title_buf, title_len);
            out.mode = std::string(mode_buf, mode_len);
            out.status = status;
            return true;
        }

        bool ConversationRepo::Touch(uint64_t conv_id, std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "UPDATE conversations SET updated_at = NOW(3) WHERE id = ?";

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

            MYSQL_BIND bind[1];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &conv_id;
            bind[0].is_unsigned = true;

            if (mysql_stmt_bind_param(stmt, bind) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            bool ok = (mysql_stmt_execute(stmt) == 0);
            if (!ok)
                err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return ok;
        }

        bool ConversationRepo::Delete(uint64_t conv_id, std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();
            const char *sql =
                "UPDATE conversations SET status = 0 WHERE id = ?";

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

            MYSQL_BIND bind[1];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &conv_id;
            bind[0].is_unsigned = true;

            if (mysql_stmt_bind_param(stmt, bind) != 0)
            {
                err = mysql_stmt_error(stmt);
                mysql_stmt_close(stmt);
                return false;
            }

            bool ok = (mysql_stmt_execute(stmt) == 0);
            if (!ok)
                err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return ok;
        }

    } // namespace ai
} // namespace tmms