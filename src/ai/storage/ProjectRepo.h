#pragma once
#include "MySQLClient.h"
#include <string>
#include <vector>
#include <cstdint>

namespace tmms
{
    namespace ai
    {
        struct ProjectRecord
        {
            uint64_t id{0};
            uint64_t user_id{0};
            std::string name;
            std::string description;
            int status{1};
            std::string created_at;
            std::string updated_at;
        };

        class ProjectRepo
        {
        public:
            explicit ProjectRepo(MySQLClient *client);

            // 创建项目，成功返回 project_id，失败返回 0
            uint64_t Create(uint64_t user_id,
                            const std::string &name,
                            const std::string &description,
                            std::string &err);

            // 列出某个用户的全部有效项目（按 updated_at 降序）
            bool ListByUser(uint64_t user_id,
                            std::vector<ProjectRecord> &out,
                            std::string &err);

            // 按 id 查询项目
            bool FindById(uint64_t project_id,
                          ProjectRecord &out,
                          std::string &err);

            // 判断项目是否存在且属于某个用户
            bool Exists(uint64_t project_id,
                        uint64_t user_id,
                        std::string &err);

            // 硬删除一个项目
            bool Delete(uint64_t project_id,
                        uint64_t user_id,
                        std::string &err);

        private:
            MySQLClient *db_;
        };

    } // namespace ai
} // namespace tmms