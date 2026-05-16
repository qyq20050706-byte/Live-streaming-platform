#include "DoubaoLLM.h"
#include "base/LogStream.h"
#include <sstream>

namespace tmms
{
    namespace ai
    {
        DoubaoLLM::DoubaoLLM(const base::AIConfigInfoPtr &cfg)
        {
            if (!cfg)
            {
                LOG_ERROR << "DoubaoLLM: null config";
                return;
            }
            base_url_   = cfg->base_url;
            api_key_    = cfg->api_key;
            model_      = cfg->model;
            timeout_ms_ = cfg->timeout_ms;

            LOG_INFO << "DoubaoLLM initialized."
                     << " model=" << model_
                     << " timeout_ms=" << timeout_ms_;
        }

        // ============================================================
        // 内部核心方法：所有接口都走这里
        // ============================================================
        bool DoubaoLLM::DoChat(const Json::Value &messages,
                                bool stream,
                                StreamCallback callback,
                                std::string &answer,
                                std::string &err)
        {
            Json::Value root;
            root["model"]  = model_;
            root["stream"] = stream;
            root["messages"] = messages;

            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::string body = Json::writeString(builder, root);

            std::vector<std::string> headers = {
                "Content-Type: application/json",
                "Authorization: Bearer " + api_key_
            };

            if (!stream)
            {
                // 普通问答
                std::string response;
                long http_code = 0;

                if (!client_.Post(base_url_, body, headers,
                                  timeout_ms_, response, http_code, err))
                {
                    LOG_ERROR << "DoubaoLLM::DoChat POST failed: " << err;
                    return false;
                }

                if (http_code != 200)
                {
                    err = "HTTP error " + std::to_string(http_code)
                          + " body=" + response;
                    LOG_ERROR << "DoubaoLLM::DoChat " << err;
                    return false;
                }

                Json::Value resp;
                Json::CharReaderBuilder rb;
                std::string parse_err;
                std::istringstream ss(response);
                if (!Json::parseFromStream(rb, ss, &resp, &parse_err))
                {
                    err = "JSON parse error: " + parse_err;
                    return false;
                }

                answer = resp["choices"][0]["message"]["content"].asString();
                LOG_DEBUG << "DoubaoLLM::DoChat success answer_len="
                          << answer.size();
                return true;
            }
            else
            {
                // 流式问答
                std::vector<std::string> stream_headers = headers;
                stream_headers.push_back("Accept: text/event-stream");

                std::string buffer;
                long http_code = 0;

                bool ret = client_.PostStream(
                    base_url_, body, stream_headers, timeout_ms_,
                    [&](const std::string &chunk)
                    {
                        buffer += chunk;

                        size_t pos;
                        while ((pos = buffer.find("\n\n")) != std::string::npos)
                        {
                            std::string line = buffer.substr(0, pos);
                            buffer.erase(0, pos + 2);

                            if (line.empty() || line.rfind("data: ", 0) != 0)
                                continue;

                            std::string json_str = line.substr(6);
                            if (json_str == "[DONE]")
                                return;

                            Json::Value resp;
                            Json::CharReaderBuilder rb;
                            std::string pe;
                            std::istringstream ss(json_str);
                            if (!Json::parseFromStream(rb, ss, &resp, &pe))
                                continue;

                            if (resp.isMember("choices") &&
                                resp["choices"].isArray() &&
                                !resp["choices"].empty() &&
                                resp["choices"][0].isMember("delta") &&
                                resp["choices"][0]["delta"].isMember("content"))
                            {
                                std::string delta =
                                    resp["choices"][0]["delta"]["content"].asString();
                                if (!delta.empty() && callback)
                                    callback(delta);
                            }
                        }
                    },
                    http_code, err);

                if (!ret)
                {
                    LOG_ERROR << "DoubaoLLM::DoChat stream failed: " << err;
                    return false;
                }

                if (http_code != 200)
                {
                    err = "HTTP error: " + std::to_string(http_code);
                    LOG_ERROR << "DoubaoLLM::DoChat " << err;
                    return false;
                }

                LOG_DEBUG << "DoubaoLLM::DoChat stream completed.";
                return true;
            }
        }

        // ============================================================
        // 单轮普通问答（原有，保持兼容，RAG 继续用这个）
        // ============================================================
        bool DoubaoLLM::Chat(const std::string &prompt, std::string &answer)
        {
            Json::Value messages(Json::arrayValue);
            Json::Value msg;
            msg["role"]    = "user";
            msg["content"] = prompt;
            messages.append(msg);

            std::string err;
            bool ok = DoChat(messages, false, nullptr, answer, err);
            if (!ok) answer = err;
            return ok;
        }

        // ============================================================
        // 单轮流式问答（原有，保持兼容）
        // ============================================================
        bool DoubaoLLM::ChatStream(const std::string &prompt,
                                    StreamCallback callback,
                                    std::string &err_msg)
        {
            Json::Value messages(Json::arrayValue);
            Json::Value msg;
            msg["role"]    = "user";
            msg["content"] = prompt;
            messages.append(msg);

            std::string answer;
            return DoChat(messages, true, callback, answer, err_msg);
        }

        // ============================================================
        // 多轮普通问答（新增）
        // ============================================================
        bool DoubaoLLM::ChatWithMessages(const Json::Value &messages,
                                          std::string &answer)
        {
            std::string err;
            bool ok = DoChat(messages, false, nullptr, answer, err);
            if (!ok) answer = err;
            return ok;
        }

        // ============================================================
        // 多轮流式问答（新增）
        // ============================================================
        bool DoubaoLLM::ChatStreamWithMessages(const Json::Value &messages,
                                                StreamCallback callback,
                                                std::string &err_msg)
        {
            std::string answer;
            return DoChat(messages, true, callback, answer, err_msg);
        }

    } // namespace ai
} // namespace tmms