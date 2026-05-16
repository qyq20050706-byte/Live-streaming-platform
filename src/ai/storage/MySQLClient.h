#pragma once
#include <mysql/mysql.h>
#include <string>
#include <memory>
#include <mutex>

namespace tmms
{
    namespace ai
    {
        struct MySQLConfig
        {
            std::string host = "127.0.0.1";
            uint16_t port = 3306;
            std::string user;
            std::string password;
            std::string database;
            std::string charset = "utf8mb4";
        };

        class MySQLClient
        {
        public:
            MySQLClient();
            ~MySQLClient();

            // 从 json 配置文件加载
            bool LoadConfig(const std::string &config_path, std::string &err);

            // 按当前配置连接
            bool Connect(std::string &err);

            // 关闭连接
            void Close();

            // 是否已连接
            bool IsConnected() const;

            // 执行非查询 SQL
            bool Execute(const std::string &sql, std::string &err);

            // 执行查询 SQL，返回结果集
            MYSQL_RES *Query(const std::string &sql, std::string &err);

            // 释放结果集
            void FreeResult(MYSQL_RES *res);

            // 获取最后插入 ID
            uint64_t LastInsertId() const;

            // 转义字符串，防止 SQL 注入
            std::string Escape(const std::string &input);

            // 获取底层句柄（后续如需 prepared statement）
            MYSQL *Raw() { return conn_; }

            std::mutex &Mutex();

        private:
            MYSQL *conn_{nullptr};
            MySQLConfig config_;
            std::mutex mutex_;
        };

    } // namespace ai
} // namespace tmms