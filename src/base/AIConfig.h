#pragma once
#include <string>
#include <memory>
#include "json/json.h"
#include "NonCopyable.h"
#include "Singleton.h"
#include <mutex>

namespace tmms{
  namespace base{
    struct AIConfigInfo{
      std::string base_url;
      std::string api_key;
      std::string model;
      int timeout_ms{5000};
    };
    using AIConfigInfoPtr = std::shared_ptr<AIConfigInfo>;

    class AIConfig{
    public:
      AIConfig() = default;
      ~AIConfig() = default;

      bool LoadConfig(const std::string &file);
      AIConfigInfoPtr& GetAIConfigInfo() { return ai_config_info_; }

    private:
      AIConfigInfoPtr ai_config_info_;
    };
    using AIConfigPtr = std::shared_ptr<AIConfig>;

    class AIConfigMgr : public NonCopyable{
    public:
      AIConfigMgr() = default;
      ~AIConfigMgr() = default;

      bool LoadConfig(const std::string &file){
        std::lock_guard<std::mutex> lock(lock_);
        ai_config_ = std::make_shared<AIConfig>();
        return ai_config_->LoadConfig(file);
      }

      AIConfigPtr GetConfig() { return ai_config_; }

    private:
      AIConfigPtr ai_config_;
      std::mutex lock_;
    };

    #define sAIConfigMgr tmms::base::Singleton<tmms::base::AIConfigMgr>::Instance()

  }
}