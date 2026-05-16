#include "base/Config.h"
#include "base/LogStream.h"
#include "base/FileMgr.h"
#include "base/Task.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <base/TaskMgr.h>

using namespace tmms::base;

int main(int argc,const char ** argv){
  if(!sConfigMgr->LoadConfig(PROJECT_ROOT "/bin/config/config.json")){
    std::cerr<<"load config file failed"<<std::endl;
    return -1;
  }
  ConfigPtr config=sConfigMgr->GetConfig();
  LogInfoPtr log_info=config->GetLogInfo();
  std::string real_log_path = PROJECT_ROOT "/bin/log/";
  std::cout<<"log level"<<log_info->level
            <<"path:"<<real_log_path
            <<"name:"<<log_info->name<<std::endl;
  FileLogPtr log=sFileMgr->GetFileLog(real_log_path+log_info->name);
  if(!log){
    std::cerr<<"log can't open.exit."<<std::endl;
    return -1;
  }
  log->SetRotate(log_info->rotate_type);
  g_logger=new Logger(log);
  g_logger->SetLogLevel(log_info->level);
  TaskPtr task4=std::make_shared<Task>([](const TaskPtr &task){
      sFileMgr->OnCheck();
      task->Restart();
      },30000);
  sTaskMgr->Add(task4);
  while(1){
    sTaskMgr->OnWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return 0;
}
