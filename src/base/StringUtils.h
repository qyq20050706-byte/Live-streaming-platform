#pragma once 
#include <string>
#include<vector>

namespace tmms{
  namespace base{
    using std::string;
    class StringUtils{
      public:
        static bool StartsWith(const string &s,const string &sub);
        static bool EndsWith(const string &s,const string &sub);
        static string FilePath(const string &path);
        static string FileNameExt(const string &path);
        static string FileName(const string &path);
        static string Extension(const string &path);
        static std::vector<string> SplitString(const string &s, const string &delimiter);
    };
  }
}
