#pragma once
#include <string>
#include <vector>

namespace tmms
{
    namespace ai
    {
        class PasswordHasher
        {
        public:
            static std::string Hash(const std::string &password,
                                    int iterations = 100000);

            static bool Verify(const std::string &password,
                               const std::string &phc_string);

        private:
            static std::string GenerateSalt();

            static std::string Base64Encode(const unsigned char *data,
                                            size_t len);

            static bool Base64Decode(const std::string &encoded,
                                     std::vector<unsigned char> &out);

            static bool DoPBKDF2(const std::string &password,
                                 const unsigned char *salt,
                                 size_t salt_len,
                                 int iterations,
                                 unsigned char *out,
                                 size_t out_len);
        };

    } // namespace ai
} // namespace tmms