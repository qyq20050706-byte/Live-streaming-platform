#include "MySQLClient.h"
#include "base/Config.h"
#include "base/LogStream.h"
#include <json/json.h>

namespace tmms
{
    namespace ai
    {
        MySQLClient::MySQLClient()
        {
        }

        MySQLClient::~MySQLClient()
        {
            Close();
        }

        bool MySQLClient::LoadConfig(const std::string &config_path,
                                     std::string &err)
        {
            Json::Value root;
            if (!tmms::base::Config::LoadFile(config_path, root))
            {
                err = "failed to load db config: " + config_path;
                return false;
            }

            config_.host = root.get("host", "127.0.0.1").asString();
            config_.port = static_cast<uint16_t>(root.get("port", 3306).asUInt());
            config_.user = root.get("user", "").asString();
            config_.password = root.get("password", "").asString();
            config_.database = root.get("database", "").asString();
            config_.charset = root.get("charset", "utf8mb4").asString();

            if (config_.user.empty() || config_.database.empty())
            {
                err = "db config missing required fields";
                return false;
            }

            LOG_INFO << "MySQL config loaded."
                     << " host=" << config_.host
                     << " port=" << config_.port
                     << " user=" << config_.user
                     << " database=" << config_.database;

            return true;
        }

        bool MySQLClient::Connect(std::string &err)
        {
            if (conn_)
            {
                return true;
            }

            conn_ = mysql_init(nullptr);
            if (!conn_)
            {
                err = "mysql_init failed";
                return false;
            }

            unsigned int connect_timeout = 10;
            mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);

            // 设置字符集
            mysql_options(conn_, MYSQL_SET_CHARSET_NAME, config_.charset.c_str());

            if (!mysql_real_connect(conn_,
                                    config_.host.c_str(),
                                    config_.user.c_str(),
                                    config_.password.c_str(),
                                    config_.database.c_str(),
                                    config_.port,
                                    nullptr,
                                    0))
            {
                err = mysql_error(conn_);
                mysql_close(conn_);
                conn_ = nullptr;
                return false;
            }

            LOG_INFO << "MySQL connected."
                     << " host=" << config_.host
                     << " port=" << config_.port
                     << " db=" << config_.database;
            return true;
        }

        void MySQLClient::Close()
        {
            if (conn_)
            {
                mysql_close(conn_);
                conn_ = nullptr;
                LOG_INFO << "MySQL connection closed.";
            }
        }

        bool MySQLClient::IsConnected() const
        {
            return conn_ != nullptr;
        }

        bool MySQLClient::Execute(const std::string &sql, std::string &err)
        {
            if (!conn_ || mysql_ping(conn_) != 0)
            {
                err = "mysql not connected or connection lost";
                LOG_ERROR << "MySQLClient: connection check failed";
                return false;
            }

            if (mysql_real_query(conn_, sql.c_str(), sql.size()) != 0)
            {
                err = mysql_error(conn_);
                LOG_ERROR << "MySQL Execute failed: " << err
                          << " sql=" << sql;
                return false;
            }

            return true;
        }

        MYSQL_RES *MySQLClient::Query(const std::string &sql, std::string &err)
        {
            if (!conn_ || mysql_ping(conn_) != 0)
            {
                err = "mysql not connected or connection lost";
                LOG_ERROR << "MySQLClient: connection check failed";
                return nullptr;
            }

            if (mysql_real_query(conn_, sql.c_str(), sql.size()) != 0)
            {
                err = mysql_error(conn_);
                LOG_ERROR << "MySQL Query failed: " << err
                          << " sql=" << sql;
                return nullptr;
            }

            MYSQL_RES *res = mysql_store_result(conn_);
            if (!res && mysql_field_count(conn_) != 0)
            {
                err = mysql_error(conn_);
                LOG_ERROR << "MySQL store_result failed: " << err;
                return nullptr;
            }

            return res;
        }

        void MySQLClient::FreeResult(MYSQL_RES *res)
        {
            if (res)
            {
                mysql_free_result(res);
            }
        }

        uint64_t MySQLClient::LastInsertId() const
        {
            if (!conn_)
                return 0;
            return static_cast<uint64_t>(mysql_insert_id(conn_));
        }

        std::string MySQLClient::Escape(const std::string &input)
        {
            if (!conn_)
                return input;

            std::string out;
            out.resize(input.size() * 2 + 1);

            unsigned long len = mysql_real_escape_string(
                conn_,
                &out[0],
                input.c_str(),
                input.size());

            out.resize(len);
            return out;
        }

        std::mutex &MySQLClient::Mutex()
        {
            return mutex_;
        }

    } // namespace ai
} // namespace tmms