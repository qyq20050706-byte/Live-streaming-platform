#pragma once
#include "MySQLClient.h"
#include <string>
#include <cstdint>
#include <memory>

namespace tmms
{
    namespace ai
    {
        struct UserRecord
        {
            uint64_t    id{0};
            std::string username;
            std::string password_hash;
            int         status{1};
        };

        class UserRepo
        {
        public:
            explicit UserRepo(MySQLClient *client);

            // 创建用户，返回新用户 id，失败返回 0
            uint64_t Create(const std::string &username,
                            const std::string &password_hash,
                            std::string &err);

            // 按用户名查找
            bool FindByUsername(const std::string &username,
                                UserRecord &out,
                                std::string &err);

            // 更新最后登录时间
            bool UpdateLastLogin(uint64_t user_id, std::string &err);

            // 检查用户名是否已存在
            bool UsernameExists(const std::string &username,
                                bool &exists,
                                std::string &err);

        private:
            MySQLClient *db_;
        };

    } // namespace ai
} // namespace tmms