#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace props {
// Opening \\host, //host, /\host, \\?\UNC\..., \??\..., \\.\... or a URL makes
// Windows connect to another computer with the user's credentials. Only a drive
// letter may start a rooted path; text that is not UTF-8 is refused as well.
// Header-only so the importers (paths written in model files) and the runtime and
// game modules (paths that come from Lua) decide with this one parser.
inline bool networkPath(std::string_view s) {
    if(s.find("://")!=s.npos) return true;
    auto separator=[](char c){return c=='\\'||c=='/';};
    if(s.size()>=2&&separator(s[0])&&separator(s[1])) return true;
    if(s.size()>=4&&separator(s[0])&&s[1]=='?'&&s[2]=='?'&&separator(s[3])) return true;
    // The UTF-8 conversion of std::filesystem throws on invalid sequences.
    std::wstring text;try{text=std::filesystem::path(std::u8string(s.begin(),s.end())).wstring();}catch(...){return true;}
    for(auto& c:text)if(c==L'/')c=L'\\';
    auto root=std::filesystem::path(text).root_name().wstring();
    return !(root.empty()||(root.size()==2&&root[1]==L':'));
}
}
