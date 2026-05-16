#pragma once
#include "MySQLClient.h"
#include <string>
#include <vector>
#include <cstdint>

namespace tmms
{
    namespace ai
    {
        struct MessageRecord
        {
            uint64_t    id{0};
            uint64_t    conversation_id{0};
            std::string role;
            std::string content;
            std::string created_at;
        };

        class MessageRepo
        {
        public:
            explicit MessageRepo(MySQLClient *client);

            // 插入一条消息，返回新消息 id
            uint64_t Insert(uint64_t conversation_id,
                            const std::string &role,
                            const std::string &content,
                            std::string &err);

            // 查询最近 N 条消息（按 created_at ASC，即时间正序）
            bool ListRecent(uint64_t conversation_id,
                            int limit,
                            std::vector<MessageRecord> &out,
                            std::string &err);

            // 查询消息总数
            int Count(uint64_t conversation_id, std::string &err);

        private:
            MySQLClient *db_;
        };

    } // namespace ai
} // namespace tmms