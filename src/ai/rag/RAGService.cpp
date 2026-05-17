#include "RAGService.h"
#include "base/LogStream.h"
#include "base/TTime.h"
#include <sstream>

namespace tmms
{
    namespace ai
    {
        RAGService::RAGService(IEmbeddingProvider *embedder,
                               IVectorStore *store,
                               ILLMProvider *llm)
            : embedder_(embedder), store_(store), llm_(llm), splitter_(300, 50)
        {
        }

        void RAGService::SetConfig(const RAGConfig &cfg)
        {
            config_ = cfg;
            splitter_ = TextSplitter(cfg.chunk_size, cfg.chunk_overlap);
        }

        int RAGService::AddKnowledge(const std::string &text,
                                     const std::string &source,
                                     std::string &err)
        {
            auto chunks = splitter_.Split(text);
            if (chunks.empty())
            {
                err = "text is empty after splitting";
                return 0;
            }

            LOG_INFO << "RAGService::AddKnowledge"
                     << " source=" << source
                     << " chunks=" << chunks.size();

            int success = 0;
            for (size_t i = 0; i < chunks.size(); ++i)
            {
                std::vector<float> embedding;
                std::string embed_err;

                if (!embedder_->Embed(chunks[i], embedding, embed_err))
                {
                    LOG_WARN << "RAGService: embed chunk " << i
                             << " failed: " << embed_err;
                    continue;
                }

                uint64_t cnt = ++chunk_counter_;
                std::ostringstream id_oss;
                id_oss << "chunk_"
                       << tmms::base::TTime::NowMS()
                       << "_" << cnt;

                // 适配新的 InsertParams
                IVectorStore::InsertParams params;
                params.id = id_oss.str();
                params.user_id = 0; // 当前兼容模式：匿名全局知识库
                params.project_id = -1;
                params.conversation_id = -1;
                params.filename = "";
                params.chunk_type = "text";
                params.start_line = 0;
                params.end_line = 0;
                params.content = chunks[i];
                params.content_hash = ""; // 当前先不做 hash，下一阶段再加
                params.source = source;

                std::string insert_err;
                if (!store_->Insert(params, embedding, insert_err))
                {
                    LOG_WARN << "RAGService: insert chunk " << i
                             << " failed: " << insert_err;
                    continue;
                }

                ++success;
                LOG_DEBUG << "RAGService: chunk " << i
                          << " stored id=" << id_oss.str();
            }

            if (success == 0)
            {
                err = "all chunks failed to store";
                return 0;
            }

            LOG_INFO << "RAGService::AddKnowledge done"
                     << " success=" << success
                     << "/" << chunks.size();
            return success;
        }

        bool RAGService::Retrieve(const std::string &query,
                                  std::vector<std::string> &contexts,
                                  std::string &err)
        {
            std::vector<float> query_emb;
            if (!embedder_->Embed(query, query_emb, err))
            {
                LOG_ERROR << "RAGService::Retrieve embed failed: " << err;
                return false;
            }

            // 适配新的 SearchParams
            IVectorStore::SearchParams params;
            params.user_id = 0; // 当前兼容模式：匿名全局知识库
            params.project_id = -1;
            params.conversation_id = -1;
            params.top_k_session = 0;
            params.top_k_project = 0;
            params.top_k_global = config_.top_k;
            params.min_similarity = config_.min_similarity;

            std::vector<VectorSearchResult> results;
            if (!store_->Search(query_emb, params, results, err))
            {
                LOG_ERROR << "RAGService::Retrieve search failed: " << err;
                return false;
            }

            LOG_INFO << "RAGService::Retrieve"
                     << " query_len=" << query.size()
                     << " hits=" << results.size();

            for (auto &r : results)
            {
                contexts.push_back(r.content);
                LOG_DEBUG << "  hit score=" << r.score
                          << " source=" << r.source
                          << " file=" << r.filename
                          << " lines=" << r.start_line << "-" << r.end_line;
            }

            return true;
        }

        bool RAGService::Chat(const std::string &query,
                              std::string &answer,
                              std::string &err)
        {
            std::vector<std::string> contexts;
            if (!Retrieve(query, contexts, err))
                return false;

            std::string full_prompt;
            if (!contexts.empty())
            {
                full_prompt = PromptTemplate::BuildSystemPrompt(contexts);
                full_prompt += "\n\n用户问题：" + query;
            }
            else
            {
                full_prompt = "用户问题：" + query + "\n\n（知识库中没有找到相关内容，请如实告知用户）";
            }

            bool ok = llm_->Chat(full_prompt, answer);
            if (!ok)
            {
                err = "LLM chat failed: " + answer;
                return false;
            }

            return true;
        }

        bool RAGService::ChatStream(
            const std::string &query,
            std::function<void(const std::string &)> callback,
            std::string &err)
        {
            // 1. 检索
            std::vector<std::string> contexts;
            if (!Retrieve(query, contexts, err))
                return false;

            // 2. 构建 messages 数组
            Json::Value messages = PromptTemplate::BuildRAGMessages(
                contexts, query);

            // 3. 流式调用
            // 直接调用 ILLMProvider 的 ChatStreamWithMessages 接口
            bool ok = llm_->ChatStreamWithMessages(messages, callback, err);
            if (!ok)
            {
                err = "LLM chat stream failed: " + err;
                return false;
            }

            return true;
        }

        int RAGService::Count()
        {
            return store_->Count();
        }

        bool RAGService::Clear(std::string &err)
        {
            return store_->Clear(err);
        }

    } // namespace ai
} // namespace tmms