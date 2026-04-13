#pragma once 
#include <string>
#include <cstdint>
#include "json/json.h"
#include <memory>
#include "NonCopyable.h"
#include "Singleton.h"
#include "Logger.h"
#include <mutex>
#include "Logger.h"

namespace tmms{
  namespace base{
    struct LogInfo{
      LogLevel level;
      std::string path;
      std::string name;
      RotateType rotate_type{kRotateNone};
    };
    using LogInfoPtr=std::shared_ptr<LogInfo>;
    class Config{
      public:
        Config()=default;
        ~Config()=default;

        bool LoadConfig(const std::string &file);
        LogInfoPtr& GetLogInfo();

        std::string name_;
        std::int32_t cpu_start_{0};
        std::int32_t thread_nums_{1};
      private:
        bool ParseLogInfo(const Json::Value & root);
        LogInfoPtr log_info_;

  };
    using ConfigPtr=std::shared_ptr<Config>;
    class ConfigMgr:public NonCopyable{
      public:
      ConfigMgr()=default;
      ~ConfigMgr()=default;

      bool LoadConfig(const std::string &file);
      ConfigPtr GetConfig();

      private:
      ConfigPtr config_;
      std::mutex lock_;
    };
#define sConfigMgr tmms::base::Singleton<tmms::base::ConfigMgr>::Instance()
}
}
