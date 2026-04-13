#pragma once 
#include "NonCopyable.h"
#include <string>
#include"FileLog.h"
namespace tmms{
  namespace base{
    
    enum LogLevel{
      kTrace,
      kDebug,
      kInfo,
      kWarn,
      kError,
      kMaxNumOfLogLevel,
    };
    class Logger:public NonCopyable{
      public:
        Logger() = default;
        Logger(const FileLogPtr &log);
        ~Logger()=default;

        void SetLogLevel(const LogLevel &level);
        LogLevel GetLogLevel() const;
        void Write(const std::string &msg);

      private:
        LogLevel level_{kDebug};
        FileLogPtr log_;
    };
  }
}
