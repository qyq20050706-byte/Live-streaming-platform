#include "DoubaoEmbedding.h"
#include "base/LogStream.h"
#include <fstream>
#include <sstream>

namespace tmms
{
    namespace ai
    {
        bool DoubaoEmbedding::Init(const std::string &config_file,
                                   std::string &err)
        {
            std::ifstream ifs(config_file);
            if (!ifs.is_open())
            {
                err = "cannot open embedding config: " + config_file;
                return false;
            }

            Json::Value root;
            Json::CharReaderBuilder reader;
            std::string errs;
            if (!Json::parseFromStream(reader, ifs, &root, &errs))
            {
                err = "embedding config parse error: " + errs;
                return false;
            }

            base_url_   = root["base_url"].asString();
            api_key_    = root["api_key"].asString();
            model_      = root["model"].asString();
            timeout_ms_ = root["timeout_ms"].asInt();
            dimension_  = root.get("dimension", 3072).asInt();

            LOG_INFO << "DoubaoEmbedding init."
                     << " model=" << model_
                     << " dimension=" << dimension_;
            return true;
        }

        bool DoubaoEmbedding::Embed(const std::string &text,
                                    std::vector<float> &embedding,
                                    std::string &err)
        {
            // 构造请求体
            Json::Value root;
            root["model"]           = model_;
            root["encoding_format"] = "float";

            Json::Value item;
            item["type"] = "text";
            item["text"] = text;
            root["input"].append(item);

            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            std::string body = Json::writeString(writer, root);

            std::vector<std::string> headers = {
                "Content-Type: application/json",
                "Authorization: Bearer " + api_key_
            };

            std::string response;
            long http_code = 0;

            if (!client_.Post(base_url_, body, headers,
                              timeout_ms_, response, http_code, err))
            {
                LOG_ERROR << "DoubaoEmbedding::Embed HTTP failed: " << err;
                return false;
            }

            if (http_code != 200)
            {
                err = "HTTP error " + std::to_string(http_code)
                      + " body=" + response;
                LOG_ERROR << "DoubaoEmbedding::Embed " << err;
                return false;
            }

            // 响应格式：{"data": {"embedding": [...], "object": "embedding"}}
            // 注意 data 是对象，不是数组
            Json::Value resp;
            Json::CharReaderBuilder rbuilder;
            std::string parse_err;
            std::istringstream ss(response);
            if (!Json::parseFromStream(rbuilder, ss, &resp, &parse_err))
            {
                err = "JSON parse error: " + parse_err;
                LOG_ERROR << "DoubaoEmbedding::Embed " << err;
                return false;
            }

            if (!resp.isMember("data") ||
                !resp["data"].isMember("embedding"))
            {
                err = "unexpected response format: " + response;
                LOG_ERROR << "DoubaoEmbedding::Embed " << err;
                return false;
            }

            const Json::Value &emb_array = resp["data"]["embedding"];
            if (!emb_array.isArray() || emb_array.empty())
            {
                err = "embedding array is empty";
                LOG_ERROR << "DoubaoEmbedding::Embed " << err;
                return false;
            }

            embedding.clear();
            embedding.reserve(emb_array.size());
            for (const auto &v : emb_array)
            {
                embedding.push_back(v.asFloat());
            }

            LOG_DEBUG << "DoubaoEmbedding::Embed ok"
                      << " text_len=" << text.size()
                      << " dim=" << embedding.size();
            return true;
        }

    } // namespace ai
} // namespace tmms