#pragma once
#include "JWT.h"
#include "ai/storage/UserRepo.h"
#include "ai/storage/MySQLClient.h"
#include <string>
#include <cstdint>

namespace tmms
{
    namespace ai
    {
        struct RegisterResult
        {
            bool ok{false};
            int code{0};
            std::string message;
            uint64_t user_id{0};
        };

        struct LoginResult
        {
            bool ok{false};
            int code{0};
            std::string message;
            uint64_t user_id{0};
            std::string username;
            std::string token;
        };

        class AuthService
        {
        public:
            AuthService(MySQLClient *db,
                        const JWTConfig &jwt_cfg,
                        int pbkdf2_iterations = 100000);
                        
            RegisterResult Register(const std::string &username,
                                    const std::string &password);

            LoginResult Login(const std::string &username,
                              const std::string &password);

            // 验证请求头里的 token
            bool VerifyToken(const std::string &token,
                             uint64_t &user_id,
                             std::string &username,
                             std::string &err);

        private:
            UserRepo user_repo_;
            JWTConfig jwt_cfg_;
            int pbkdf2_iterations_{100000};
        };

    } // namespace ai
} // namespace tmms