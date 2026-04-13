#include "StringUtils.h"
#include <vector>
#include <string>

using namespace std;
using namespace tmms::base;

bool StringUtils::StartsWith(const string &s, const string &sub) {
    if (sub.empty()) return true;
    if (s.empty()) return false;

    auto len = s.size();
    auto slen = sub.size();

    if (slen > len) return false;

    return s.compare(0, slen, sub) == 0;
}

bool StringUtils::EndsWith(const string &s, const string &sub) {
    if (sub.empty()) return true;
    if (s.empty()) return false;

    auto len = s.size();
    auto slen = sub.size();

    if (slen > len) return false;

    return s.compare(len - slen, slen, sub) == 0;
}

string StringUtils::FilePath(const string &path) {
    auto pos = path.find_last_of("/\\");
    if (pos != string::npos) {
        return path.substr(0, pos);
    } else {
        return "./";
    }
}

string StringUtils::FileNameExt(const string &path) {
    auto pos = path.find_last_of("/\\");
    if (pos != string::npos) {
        if (pos + 1 < path.size()) {
            return path.substr(pos + 1);
        }
    }
    return path;
}

string StringUtils::FileName(const string &path) {
    string file_name = FileNameExt(path);
    auto pos = file_name.find_last_of(".");
    if (pos != string::npos) {
        if (pos != 0) {
            return file_name.substr(0, pos);
        }
    }
    return file_name;
}

string StringUtils::Extension(const string &path) {
    string file_name = FileNameExt(path);
    auto pos = file_name.find_last_of(".");
    if (pos != string::npos) {
        if (pos != 0 && pos+1 < file_name.size()) {
            return file_name.substr(pos+1);
        }
    }
    return string();
}

std::vector<string> StringUtils::SplitString(const string &s, const string &delimiter) {
    std::vector<string> res;
    if (s.empty() || delimiter.empty()) return res;

    string str = s;
    size_t pos = 0;

    while ((pos = str.find(delimiter)) != string::npos) {
        res.emplace_back(str.substr(0, pos));
        str = str.substr(pos + delimiter.size());
    }
    if (!str.empty()) {
        res.emplace_back(str);
    }
    return res;
}
