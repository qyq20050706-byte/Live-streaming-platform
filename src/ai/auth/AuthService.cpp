#include "AuthService.h"
#include "PasswordHasher.h"
#include "base/LogStream.h"

namespace tmms
{
    namespace ai
    {
        AuthService::AuthService(MySQLClient *db,
                                 const JWTConfig &jwt_cfg,
                                 int pbkdf2_iterations)
            : user_repo_(db), jwt_cfg_(jwt_cfg), pbkdf2_iterations_(pbkdf2_iterations)
        {
        }

        RegisterResult AuthService::Register(const std::string &username,
                                             const std::string &password)
        {
            RegisterResult result;

            // 1. 基本校验
            if (username.empty() || username.size() > 64)
            {
                result.code = 400;
                result.message = "invalid username";
                return result;
            }
            if (password.size() < 6)
            {
                result.code = 400;
                result.message = "password too short (min 6)";
                return result;
            }

            // 2. 检查用户名是否已存在
            bool exists = false;
            std::string err;
            if (!user_repo_.UsernameExists(username, exists, err))
            {
                LOG_ERROR << "AuthService::Register UsernameExists failed: " << err;
                result.code = 500;
                result.message = "internal error";
                return result;
            }
            if (exists)
            {
                result.code = 400;
                result.message = "username already exists";
                return result;
            }

            // 3. 生成密码哈希（PHC 格式）
            std::string phc = PasswordHasher::Hash(password, pbkdf2_iterations_);
            if (phc.empty())
            {
                LOG_ERROR << "AuthService::Register PasswordHasher::Hash failed";
                result.code = 500;
                result.message = "internal error";
                return result;
            }

            // 4. 写入数据库
            uint64_t new_id = user_repo_.Create(username, phc, err);
            if (new_id == 0)
            {
                LOG_ERROR << "AuthService::Register Create failed: " << err;
                result.code = 500;
                result.message = "internal error";
                return result;
            }

            LOG_INFO << "AuthService::Register success"
                     << " username=" << username
                     << " user_id=" << new_id;

            result.ok = true;
            result.code = 0;
            result.message = "ok";
            result.user_id = new_id;
            return result;
        }

        LoginResult AuthService::Login(const std::string &username,
                                       const std::string &password)
        {
            LoginResult result;

            // 1. 查找用户
            UserRecord record;
            std::string err;
            if (!user_repo_.FindByUsername(username, record, err))
            {
                result.code = 401;
                result.message = "username or password incorrect";
                return result;
            }

            // 2. 检查账号状态
            if (record.status != 1)
            {
                result.code = 403;
                result.message = "account disabled";
                return result;
            }

            // 3. 验证密码
            if (!PasswordHasher::Verify(password, record.password_hash))
            {
                result.code = 401;
                result.message = "username or password incorrect";
                return result;
            }

            // 4. 签发 token
            std::string token = JWT::Sign(record.id, record.username, jwt_cfg_);
            if (token.empty())
            {
                LOG_ERROR << "AuthService::Login JWT::Sign failed";
                result.code = 500;
                result.message = "internal error";
                return result;
            }

            // 5. 更新最后登录时间（失败不影响登录）
            user_repo_.UpdateLastLogin(record.id, err);

            LOG_INFO << "AuthService::Login success"
                     << " username=" << username
                     << " user_id=" << record.id;

            result.ok = true;
            result.code = 0;
            result.message = "ok";
            result.user_id = record.id;
            result.username = record.username;
            result.token = token;
            return result;
        }

        bool AuthService::VerifyToken(const std::string &token,
                                      uint64_t &user_id,
                                      std::string &username,
                                      std::string &err)
        {
            return JWT::Verify(token, jwt_cfg_, user_id, username, err);
        }

    } // namespace ai
} // namespace tmms