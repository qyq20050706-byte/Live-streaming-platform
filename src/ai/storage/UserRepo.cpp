#include "UserRepo.h"
#include "base/LogStream.h"
#include <mysql/mysql.h>
#include <cstring>
#include <mutex>

namespace tmms
{
    namespace ai
    {
        UserRepo::UserRepo(MySQLClient *client)
            : db_(client)
        {
        }

        uint64_t UserRepo::Create(const std::string &username,
                                  const std::string &password_hash,
                                  std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();

            const char *sql =
                "INSERT INTO users (username, password_hash, status) "
                "VALUES (?, ?, 1)";

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

            MYSQL_BIND bind[2];
            memset(bind, 0, sizeof(bind));

            unsigned long uname_len = username.size();
            unsigned long phash_len = password_hash.size();

            bind[0].buffer_type = MYSQL_TYPE_STRING;
            bind[0].buffer = (void *)username.c_str();
            bind[0].buffer_length = username.size();
            bind[0].length = &uname_len;

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (void *)password_hash.c_str();
            bind[1].buffer_length = password_hash.size();
            bind[1].length = &phash_len;

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

            LOG_INFO << "UserRepo::Create success username=" << username
                     << " id=" << new_id;
            return new_id;
        }

        bool UserRepo::FindByUsername(const std::string &username,
                                      UserRecord &out,
                                      std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();

            const char *sql =
                "SELECT id, username, password_hash, status "
                "FROM users WHERE username = ? LIMIT 1";

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

            // 绑定参数
            MYSQL_BIND param[1];
            memset(param, 0, sizeof(param));
            unsigned long uname_len = username.size();
            param[0].buffer_type = MYSQL_TYPE_STRING;
            param[0].buffer = (void *)username.c_str();
            param[0].buffer_length = username.size();
            param[0].length = &uname_len;

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

            // 绑定结果
            uint64_t id{0};
            char uname_buf[257] = {};
            char phash_buf[257] = {};
            int status{0};
            unsigned long id_len, un_len, ph_len, st_len;
            bool is_null[4] = {};
            bool error_flags[4] = {};

            MYSQL_BIND result[4];
            memset(result, 0, sizeof(result));

            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &id;
            result[0].is_unsigned = true;
            result[0].length = &id_len;
            result[0].is_null = (bool *)&is_null[0];
            result[0].error = (bool *)&error_flags[0];

            result[1].buffer_type = MYSQL_TYPE_STRING;
            result[1].buffer = uname_buf;
            result[1].buffer_length = sizeof(uname_buf) - 1;
            result[1].length = &un_len;
            result[1].is_null = (bool *)&is_null[1];
            result[1].error = (bool *)&error_flags[1];

            result[2].buffer_type = MYSQL_TYPE_STRING;
            result[2].buffer = phash_buf;
            result[2].buffer_length = sizeof(phash_buf) - 1;
            result[2].length = &ph_len;
            result[2].is_null = (bool *)&is_null[2];
            result[2].error = (bool *)&error_flags[2];

            result[3].buffer_type = MYSQL_TYPE_TINY;
            result[3].buffer = &status;
            result[3].length = &st_len;
            result[3].is_null = (bool *)&is_null[3];
            result[3].error = (bool *)&error_flags[3];

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
                err = "user not found";
                return false;
            }

            if (fetch_ret != 0)
            {
                err = "fetch failed";
                return false;
            }

            out.id = id;
            out.username = std::string(uname_buf, un_len);
            out.password_hash = std::string(phash_buf, ph_len);
            out.status = status;

            return true;
        }

        bool UserRepo::UpdateLastLogin(uint64_t user_id, std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            MYSQL *conn = db_->Raw();

            const char *sql =
                "UPDATE users SET last_login_at = NOW(3) WHERE id = ?";

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
            bind[0].buffer = &user_id;
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

        bool UserRepo::UsernameExists(const std::string &username,
                                      bool &exists,
                                      std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());

            exists = false;

            MYSQL *conn = db_->Raw();

            const char *sql =
                "SELECT COUNT(*) FROM users WHERE username = ?";

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
            unsigned long uname_len = username.size();
            param[0].buffer_type = MYSQL_TYPE_STRING;
            param[0].buffer = (void *)username.c_str();
            param[0].buffer_length = username.size();
            param[0].length = &uname_len;

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

            int64_t count = 0;
            unsigned long count_len;
            bool is_null = false, error_flag = false;

            MYSQL_BIND result[1];
            memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            result[0].buffer = &count;
            result[0].length = &count_len;
            result[0].is_null = &is_null;
            result[0].error = &error_flag;

            mysql_stmt_bind_result(stmt, result);
            mysql_stmt_fetch(stmt);
            mysql_stmt_close(stmt);

            exists = (count > 0);
            return true;
        }

    } // namespace ai
} // namespace tmms