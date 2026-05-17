#pragma once
#include "MySQLClient.h"
#include <string>
#include <vector>
#include <cstdint>

namespace tmms
{
    namespace ai
    {
        struct ConversationRecord
        {
            uint64_t id{0};
            uint64_t user_id{0};
            std::string title;
            std::string mode;
            int status{1};
            std::string created_at;
            std::string updated_at;
        };

        class ConversationRepo
        {
        public:
            explicit ConversationRepo(MySQLClient *client);

            // 创建会话，返回新会话 id，失败返回 0
            uint64_t Create(uint64_t user_id,
                            const std::string &title,
                            const std::string &mode,
                            std::string &err);

            // 查询用户的会话列表（按 updated_at 降序）
            bool ListByUser(uint64_t user_id,
                            int limit,
                            std::vector<ConversationRecord> &out,
                            std::string &err);

            // 查询单个会话（验证归属权用）
            bool FindById(uint64_t conv_id,
                          ConversationRecord &out,
                          std::string &err);

            // 更新 updated_at（插入消息时调用）
            bool Touch(uint64_t conv_id, std::string &err);

            // 软删除
            bool Delete(uint64_t conv_id, std::string &err);

        private:
            MySQLClient *db_;
        };

    } // namespace ai
} // namespace tmms