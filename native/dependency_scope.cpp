#include "dependency_scope.hpp"
#include "props/network_path.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <stdexcept>

namespace mmd {
namespace fs=std::filesystem;
namespace {
std::function<bool(const fs::path&)>& denylist(){static std::function<bool(const fs::path&)> denied;return denied;}
std::atomic_bool unknownFinals=false;
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::wstring fromUtf8(std::string_view s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 path");
    std::wstring out(size_t(n),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
// The extended form opens exactly the path checked: Windows does not drop trailing
// dots and spaces or treat CON and NUL as devices in it.
std::wstring extended(const fs::path& p){
    std::error_code error;auto full=fs::absolute(p,error);if(error)return {};
    auto v=full.lexically_normal().wstring();
    if(v.starts_with(L"\\\\?\\"))return v;
    if(v.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+v.substr(2);
    return L"\\\\?\\"+v;
}
// Where an open file or folder really is: \\?\C:\x -> C:\x, \\?\UNC\host\share -> \\host\share.
// Empty where Windows cannot say: some drivers (RAM disks, FUSE and cloud drives) answer
// ERROR_INVALID_FUNCTION, ERROR_NOT_SUPPORTED or ERROR_INVALID_PARAMETER.
std::wstring finalPath(HANDLE h){
    if(unknownFinals)return {};
    std::wstring b(512,L'\0');
    for(;;){DWORD n=GetFinalPathNameByHandleW(h,b.data(),DWORD(b.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!n)return {};if(n<b.size()){b.resize(n);break;}b.resize(size_t(n)+1);}
    if(b.starts_with(L"\\\\?\\UNC\\"))return L"\\\\"+b.substr(8);
    if(b.starts_with(L"\\\\?\\"))return b.substr(4);
    return b;
}
// Windows compares names case-insensitively with its own upper-case table.
bool sameText(std::wstring_view a,std::wstring_view b){return a.size()==b.size()&&(a.empty()||CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL);}
std::wstring trimmed(std::wstring s){for(auto& c:s)if(c==L'/')c=L'\\';while(s.size()>1&&s.back()==L'\\'&&!(s.size()==3&&s[1]==L':'))s.pop_back();if(s.size()==3&&s[1]==L':')s.pop_back();return s;}
bool within(const std::wstring& path,const std::wstring& folder){
    auto p=trimmed(path),f=trimmed(folder);if(f.empty()||p.size()<f.size())return false;
    return sameText(std::wstring_view(p).substr(0,f.size()),f)&&(p.size()==f.size()||p[f.size()]==L'\\');
}
// Parts Windows would read differently than written: an alternate data stream (x.png:y,
// or a download's Zone.Identifier) and names ending in a dot or space, which Win32 drops.
bool plainParts(const fs::path& p){
    for(auto& part:p.relative_path()){auto s=part.wstring();if(s.empty())continue;
        if(s==L"."||s==L".."||s.find(L':')!=s.npos||s.back()==L'.'||s.back()==L' ')return false;}
    return true;
}
std::wstring absoluteText(const fs::path& p){std::error_code error;auto full=fs::absolute(p,error);return error?std::wstring():full.lexically_normal().wstring();}
// Not a link or junction itself (a name surrogate: Windows would continue elsewhere).
// Other reparse points (OneDrive placeholders, deduplicated or compressed files) are
// the file itself.
bool notLink(const std::wstring& path){
    auto io=extended(path);if(io.empty())return false;
    auto attributes=GetFileAttributesW(io.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)return false;
    if(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return true;
    Handle h;h.h=CreateFileW(io.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h.h==INVALID_HANDLE_VALUE)return false;
    FILE_ATTRIBUTE_TAG_INFO tag{};if(!GetFileInformationByHandleEx(h.h,FileAttributeTagInfo,&tag,sizeof(tag)))return false;
    return !IsReparseTagNameSurrogate(tag.ReparseTag);
}
// `file` (absolute, normalized) as written lies in `tree`, and nothing from the tree down
// to the file is a link or junction, so the file really is there. The model's own folder
// is taken as it is (a link to it counts as the folder, as with final paths); a tex or
// textures folder near it must not be a link itself.
bool linkFreeBelow(const std::wstring& tree,const std::wstring& file,bool checkTree){
    auto folder=trimmed(tree),text=trimmed(file);
    if(!within(text,folder)||text.size()==folder.size())return false;
    if(checkTree&&!notLink(folder))return false;
    for(size_t at=folder.size()+1;;){
        auto next=text.find(L'\\',at);
        if(!notLink(text.substr(0,next)))return false;
        if(next==std::wstring::npos)return true;
        at=next+1;
    }
}
// The long form of a path as written (no 8.3 short names), for the denylist.
std::wstring longName(const std::wstring& path){
    auto io=extended(path);if(io.empty())return path;
    std::wstring b(io.size()+64,L'\0');
    for(;;){DWORD n=GetLongPathNameW(io.c_str(),b.data(),DWORD(b.size()));if(!n)return path;if(n<b.size()){b.resize(n);break;}b.resize(size_t(n)+1);}
    if(b.starts_with(L"\\\\?\\UNC\\"))return L"\\\\"+b.substr(8);
    if(b.starts_with(L"\\\\?\\"))return b.substr(4);
    return b;
}
}
void setDependencyDenylist(std::function<bool(const fs::path&)> denied){denylist()=std::move(denied);}
void simulateUnknownFinalPaths(bool unknown){unknownFinals=unknown;}
DependencyScope::DependencyScope(path modelFolder,bool textureFolders,Reach r):root(std::move(modelFolder)),reach(r){
    trees={root};
    if(textureFolders)for(auto base:{root.parent_path(),root.parent_path().parent_path()})for(auto name:{L"tex",L"textures"})trees.push_back(base/name);
}
const std::vector<std::wstring>& DependencyScope::finalTrees() const {
    if(resolved)return finals;resolved=true;
    for(size_t i=0;i<trees.size();i++){
        auto io=extended(trees[i]);if(io.empty())continue;
        Handle h;h.h=CreateFileW(io.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|(i?FILE_FLAG_OPEN_REPARSE_POINT:0),nullptr);
        if(h.h==INVALID_HANDLE_VALUE)continue;
        FILE_ATTRIBUTE_TAG_INFO tag{};if(!GetFileInformationByHandleEx(h.h,FileAttributeTagInfo,&tag,sizeof(tag))||!(tag.FileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        // A tex or textures folder near the model counts as itself, never where a link or junction leads.
        if(i&&(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&IsReparseTagNameSurrogate(tag.ReparseTag))continue;
        auto real=finalPath(h.h);if(!real.empty())finals.push_back(real);
    }
    return finals;
}
DependencyScope::path DependencyScope::locate(std::string_view reference) const {
    if(reference.empty()||props::networkPath(reference))return {};
    path ref;try{ref=path(fromUtf8(reference));}catch(...){return {};}
    path candidate;
    if(ref.is_absolute())candidate=ref.lexically_normal();
    else if(ref.has_root_name()||ref.has_root_directory())return {};  // C:x and \x depend on the current drive and folder
    else candidate=(root/ref).lexically_normal();
    if(!plainParts(candidate))return {};
    auto text=absoluteText(candidate);if(text.empty())return {};
    if(reach==Reach::Local)return candidate;
    for(auto& tree:trees)if(within(text,absoluteText(tree)))return candidate;
    return {};
}
bool DependencyScope::allows(const path& file) const {
    if(!plainParts(file))return false;
    auto text=absoluteText(file);if(text.empty())return false;
    std::vector<size_t> holders;for(size_t i=0;i<trees.size();i++)if(within(text,absoluteText(trees[i])))holders.push_back(i);
    if(holders.empty()&&reach==Reach::Confined)return false;
    auto io=extended(file);if(io.empty())return false;
    Handle h;h.h=CreateFileW(io.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h.h==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h.h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    // Links and junctions are followed by Windows: where the file really is decides.
    auto real=finalPath(h.h);
    bool inside=reach==Reach::Local;if(!inside&&!real.empty())for(auto& tree:finalTrees())inside=inside||within(real,tree);
    // Where Windows cannot say, the path as written counts when no link lies on the way.
    if(!inside)for(auto i:holders)inside=inside||linkFreeBelow(absoluteText(trees[i]),text,i>0);
    if(!inside)return false;
    auto& denied=denylist();return !denied||!denied(path(real.empty()?longName(text):real));
}
}
