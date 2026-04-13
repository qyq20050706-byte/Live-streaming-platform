#pragma once
#include "Task.h"
#include "NonCopyable.h"
#include "Singleton.h"
#include <mutex>
#include <unordered_set>

namespace tmms{
  namespace base{
    class TaskMgr:public NonCopyable{
      public:
        TaskMgr()=default;
        ~TaskMgr()=default;

        void OnWork();
        bool Add(TaskPtr &task);
        bool Del(TaskPtr &task);

      private:
        std::unordered_set<TaskPtr> tasks_;
        std::mutex lock_;
    };
  }
}
#define sTaskMgr (tmms::base::Singleton<tmms::base::TaskMgr>::Instance())

