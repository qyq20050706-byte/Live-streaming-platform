#include "TaskMgr.h"
#include "TTime.h"
#include <iostream>
#include <thread>
#include <chrono>
using namespace tmms::base;

void TestTask(){
  TaskPtr task1=std::make_shared<Task>([](const TaskPtr &task){
      std::cout<<"task1 interval:"<<1000<<std::endl;
      task->Run();
      },1000);
  TaskPtr task2=std::make_shared<Task>([](const TaskPtr &task){
           std::cout<<"task2 interval:"<<1000<<std::endl;
           task->Restart();
           },1000);
  sTaskMgr->Add(task1);
  sTaskMgr->Add(task2);

}

//int main(){
//  TestTask();
 // while(1){
 //   sTaskMgr->OnWork();
 //   std::this_thread::sleep_for(std::chrono::microseconds(50));
 // }
 // return 0;
//}
