#include "ProjectRepo.h"
#include "base/LogStream.h"
#include <mysql/mysql.h>
#include <cstring>
#include <mutex>

namespace tmms
{
    namespace ai
    {
        ProjectRepo::ProjectRepo(MySQLClient *client)
            : db_(client)
        {
        }

        uint64_t ProjectRepo::Create(uint64_t user_id,
                                     const std::string &name,
                                     const std::string &description,
                                     std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            MYSQL *conn = db_->Raw();

            const char *sql =
                "INSERT INTO rag_projects (user_id, name, description, status)"
                " VALUES (?, ?, ?, 1)";

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

            unsigned long name_len = name.size();
            unsigned long desc_len = description.size();

            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &user_id;
            bind[0].is_unsigned = true;

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (void *)name.c_str();
            bind[1].buffer_length = name.size();
            bind[1].length = &name_len;

            bind[2].buffer_type = MYSQL_TYPE_STRING;
            bind[2].buffer = (void *)description.c_str();
            bind[2].buffer_length = description.size();
            bind[2].length = &desc_len;

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

            LOG_INFO << "ProjectRepo::Create id=" << new_id
                     << " user_id=" << user_id
                     << " name=" << name;
            return new_id;
        }

        bool ProjectRepo::ListByUser(uint64_t user_id,
                                     std::vector<ProjectRecord> &out,
                                     std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            MYSQL *conn = db_->Raw();

            const char *sql =
                "SELECT id, user_id, name, description, status,"
                " DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s'),"
                " DATE_FORMAT(updated_at,'%Y-%m-%d %H:%i:%s')"
                " FROM rag_projects"
                " WHERE user_id=? AND status=1"
                " ORDER BY updated_at DESC";

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
            param[0].buffer = &user_id;
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

            // 绑定结果
            uint64_t id{0}, uid{0};
            char name_buf[129] = {};
            char desc_buf[513] = {};
            char cr_buf[32] = {};
            char up_buf[32] = {};
            int status{0};

            unsigned long id_len, uid_len, name_len, desc_len,
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
            result[2].buffer = name_buf;
            result[2].buffer_length = sizeof(name_buf) - 1;
            result[2].length = &name_len;
            result[2].is_null = &is_null[2];
            result[2].error = &err_flags[2];

            result[3].buffer_type = MYSQL_TYPE_STRING;
            result[3].buffer = desc_buf;
            result[3].buffer_length = sizeof(desc_buf) - 1;
            result[3].length = &desc_len;
            result[3].is_null = &is_null[3];
            result[3].error = &err_flags[3];

            result[4].buffer_type = MYSQL_TYPE_TINY;
            result[4].buffer = &status;
            result[4].length = &st_len;
            result[4].is_null = &is_null[4];
            result[4].error = &err_flags[4];

            result[5].buffer_type = MYSQL_TYPE_STRING;
            result[5].buffer = cr_buf;
            result[5].buffer_length = sizeof(cr_buf) - 1;
            result[5].length = &cr_len;
            result[5].is_null = &is_null[5];
            result[5].error = &err_flags[5];

            result[6].buffer_type = MYSQL_TYPE_STRING;
            result[6].buffer = up_buf;
            result[6].buffer_length = sizeof(up_buf) - 1;
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

                ProjectRecord rec;
                rec.id = id;
                rec.user_id = uid;
                rec.name = std::string(name_buf, name_len);
                rec.description = std::string(desc_buf, desc_len);
                rec.status = status;
                rec.created_at = std::string(cr_buf, cr_len);
                rec.updated_at = std::string(up_buf, up_len);
                out.push_back(std::move(rec));
            }

            mysql_stmt_close(stmt);
            return true;
        }

        bool ProjectRepo::FindById(uint64_t project_id,
                                   ProjectRecord &out,
                                   std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            MYSQL *conn = db_->Raw();

            const char *sql =
                "SELECT id, user_id, name, description, status"
                " FROM rag_projects WHERE id=? AND status=1 LIMIT 1";

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
            param[0].buffer = &project_id;
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
            char name_buf[129] = {};
            char desc_buf[513] = {};
            int status{0};

            unsigned long id_len, uid_len, name_len, desc_len, st_len;
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
            result[2].buffer = name_buf;
            result[2].buffer_length = sizeof(name_buf) - 1;
            result[2].length = &name_len;
            result[2].is_null = &is_null[2];
            result[2].error = &err_flags[2];

            result[3].buffer_type = MYSQL_TYPE_STRING;
            result[3].buffer = desc_buf;
            result[3].buffer_length = sizeof(desc_buf) - 1;
            result[3].length = &desc_len;
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
                err = "project not found";
                return false;
            }
            if (fetch_ret != 0)
            {
                err = "fetch failed";
                return false;
            }

            out.id = id;
            out.user_id = uid;
            out.name = std::string(name_buf, name_len);
            out.description = std::string(desc_buf, desc_len);
            out.status = status;
            return true;
        }

        bool ProjectRepo::Exists(uint64_t project_id,
                                 uint64_t user_id,
                                 std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            MYSQL *conn = db_->Raw();

            const char *sql =
                "SELECT COUNT(*) FROM rag_projects"
                " WHERE id=? AND user_id=? AND status=1";

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
            param[0].buffer_type = MYSQL_TYPE_LONGLONG;
            param[0].buffer = &project_id;
            param[0].is_unsigned = true;

            param[1].buffer_type = MYSQL_TYPE_LONGLONG;
            param[1].buffer = &user_id;
            param[1].is_unsigned = true;

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

            return (count > 0);
        }

        bool ProjectRepo::Delete(uint64_t project_id,
                                 uint64_t user_id,
                                 std::string &err)
        {
            std::lock_guard<std::mutex> lk(db_->Mutex());
            MYSQL *conn = db_->Raw();

            // 硬删除：测试阶段直接删
            const char *sql =
                "DELETE FROM rag_projects WHERE id=? AND user_id=?";

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

            MYSQL_BIND bind[2];
            memset(bind, 0, sizeof(bind));

            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &project_id;
            bind[0].is_unsigned = true;

            bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[1].buffer = &user_id;
            bind[1].is_unsigned = true;

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

            if (ok)
            {
                LOG_INFO << "ProjectRepo::Delete project_id=" << project_id
                         << " user_id=" << user_id;
            }

            return ok;
        }

    } // namespace ai
} // namespace tmms