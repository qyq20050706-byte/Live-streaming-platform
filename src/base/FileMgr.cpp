#include "FileMgr.h"
#include "TTime.h"
#include "StringUtils.h"
#include <sstream>
using namespace tmms::base;

namespace {
	static tmms::base::FileLogPtr file_log_nullptr;
}

void FileMgr::OnCheck(){
	int year,month,day,hour,minute,second;
	TTime::Now(year,month,day,hour,minute,second);

	std::lock_guard<std::mutex> lk(lock_);
	bool day_change=false,hour_change=false,minute_change=false;
	if(last_day_==-1){
		last_day_=day;
		last_hour_=hour;
		last_year_=year;
		last_month_=month;
		last_minute_=minute;
	}
	if(last_day_!=day){
		day_change=true;
	}
	if(last_hour_!=hour){
		hour_change=true;
	}
	if(last_minute_!=minute){
		minute_change=true;
	}
	if(!day_change && !hour_change && !minute_change){
		return ;
	}
	for(auto &l:logs_){
		if(hour_change&&l.second->GetRotateType()==kRotateHour){
			RotateHours(l.second);
		}
		if(day_change&& l.second->GetRotateType()==kRotateDay){
			RotateDay(l.second);
		}
		if(minute_change && l.second->GetRotateType()==kRotateMinute){
			RotateMinute(l.second);
		}
	}
	last_day_=day;
	last_year_=year;
	last_hour_=hour;
	last_month_=month;
	last_minute_=minute;
}

FileLogPtr FileMgr::GetFileLog(const std::string &file_name){
	std::lock_guard<std::mutex> lk(lock_);
	auto iter=logs_.find(file_name);
	if(iter!=logs_.end()){
		return iter->second;
	}
	FileLogPtr log=std::make_shared<FileLog>();
	if(!log->Open(file_name)){
		return file_log_nullptr;
	}
	logs_.emplace(file_name,log);
	return log;
}

void FileMgr::RemoveFileLog(const FileLogPtr &log){
	if(!log) return;
	std::lock_guard<std::mutex> lk(lock_);
	logs_.erase(log->FilePath());
}

void FileMgr::RotateDay(const FileLogPtr &file){
	if(file->FileSize()>0){
		char buf[128]={0};
		sprintf(buf,"_%04d-%02d-%02d",last_year_,last_month_,last_day_);
		std::string file_path=file->FilePath();
		std::string file_name=StringUtils::FileName(file_path);
		std::string path=StringUtils::FilePath(file_path);
		std::string file_ext=StringUtils::Extension(file_path);

		std::ostringstream ss;
		ss<<path<<"/"<<file_name<<buf;
		if (!file_ext.empty()) {
			ss << "." << file_ext;
		}
		file->Rotate(ss.str());
	}
}

void FileMgr::RotateHours(const FileLogPtr &file){
	if(file->FileSize()>0){
		char buf[128]={0};
		sprintf(buf,"_%04d-%02d-%02dT%02d",last_year_,last_month_,last_day_,last_hour_);
		std::string file_path=file->FilePath();
		std::string file_name=StringUtils::FileName(file_path);
		std::string path=StringUtils::FilePath(file_path);
		std::string file_ext=StringUtils::Extension(file_path);

		std::ostringstream ss;
		ss<<path<<"/"<<file_name<<buf;
		if (!file_ext.empty()) {
			ss << "." << file_ext;
		}
		file->Rotate(ss.str());
	}
}

void FileMgr::RotateMinute(const FileLogPtr &file){
	if(file->FileSize()>0){
		char buf[128]={0};
		sprintf(buf,"_%04d-%02d-%02dT%02d%02d",last_year_,last_month_,last_day_,last_hour_,last_minute_);
		std::string file_path=file->FilePath();
		std::string file_name=StringUtils::FileName(file_path);
		std::string path=StringUtils::FilePath(file_path);
		std::string file_ext=StringUtils::Extension(file_path);

		std::ostringstream ss;
		ss<<path<<"/"<<file_name<<buf;
		if (!file_ext.empty()) {
			ss << "." << file_ext;
		}
		file->Rotate(ss.str());
	}
}

