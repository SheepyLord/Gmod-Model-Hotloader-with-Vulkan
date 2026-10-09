#include "texture_resolver.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

namespace props {
namespace {
std::wstring key(std::wstring s){
    for(auto& c:s){if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(c==L' ')c=L'_';}
    auto imageExtension=[](std::wstring_view ext){return ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"tga"||ext==L"bmp";};
    auto dot=s.rfind(L'.');
    if(dot!=s.npos&&dot>=5&&imageExtension(std::wstring_view(s).substr(dot+1))&&s[dot-4]==L'.'&&
       s[dot-3]>=L'0'&&s[dot-3]<=L'9'&&s[dot-2]>=L'0'&&s[dot-2]<=L'9'&&s[dot-1]>=L'0'&&s[dot-1]<=L'9'){
        auto before=s.rfind(L'.',dot-5);
        if(before!=s.npos&&imageExtension(std::wstring_view(s).substr(before+1,dot-before-5)))s.resize(dot-4);
    }
    if(s.ends_with(L".jpeg"))s.replace(s.size()-5,5,L".jpg");
    return s;
}
std::function<bool(const fs::path&)>& denylist(){static std::function<bool(const fs::path&)> denied;return denied;}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
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
std::wstring finalPath(HANDLE h){
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
}
void setDependencyDenylist(std::function<bool(const fs::path&)> denied){denylist()=std::move(denied);}
DependencyScope::DependencyScope(fs::path modelFolder,bool textureFolders):root(std::move(modelFolder)){
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
fs::path DependencyScope::locate(std::string_view reference) const {
    if(reference.empty()||networkPath(reference))return {};
    fs::path ref;try{ref=fs::path(wide(reference));}catch(...){return {};}
    fs::path candidate;
    if(ref.is_absolute())candidate=ref.lexically_normal();
    else if(ref.has_root_name()||ref.has_root_directory())return {};  // C:x and \x depend on the current drive and folder
    else candidate=(root/ref).lexically_normal();
    if(!plainParts(candidate))return {};
    auto text=absoluteText(candidate);if(text.empty())return {};
    for(auto& tree:trees)if(within(text,absoluteText(tree)))return candidate;
    return {};
}
bool DependencyScope::allows(const fs::path& file) const {
    if(!plainParts(file))return false;
    auto text=absoluteText(file);if(text.empty())return false;
    bool written=false;for(auto& tree:trees)written=written||within(text,absoluteText(tree));
    if(!written)return false;
    auto io=extended(file);if(io.empty())return false;
    Handle h;h.h=CreateFileW(io.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h.h==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h.h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    // Links and junctions are followed by Windows: where the file really is decides.
    auto real=finalPath(h.h);if(real.empty())return false;
    bool inside=false;for(auto& tree:finalTrees())inside=inside||within(real,tree);
    if(!inside)return false;
    auto& denied=denylist();return !denied||!denied(fs::path(real));
}
TextureResolver::TextureResolver(fs::path directory):root(std::move(directory)),scope(root,true){
    folders={root,root/L"tex",root/L"textures",root.parent_path()/L"tex",root.parent_path()/L"textures",
        root.parent_path().parent_path()/L"tex",root.parent_path().parent_path()/L"textures"};
}
void TextureResolver::index(){
    if(indexed){if(!indexFailure.empty())throw std::runtime_error(indexFailure);return;}indexed=true;
    // Deliberately bounded, nonrecursive package lookup. No scans across drives
    // or arbitrary parents, and ambiguous aliases are reported, never guessed.
    size_t count=0;
    for(const auto& folder:folders){
        std::error_code error;fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,error),end;
        for(;!error&&it!=end;it.increment(error)){
            if(++count>8192){names.clear();indexFailure="Texture resource folders exceed the 8192-entry lookup limit";throw std::runtime_error(indexFailure);}
            if(!it->is_regular_file(error))continue;
            names[key(it->path().filename().wstring())].push_back(it->path().lexically_normal());
        }
    }
    for(auto& [name,paths]:names){std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());}
}
ResolvedTexture TextureResolver::resolve(const std::string& reference){
    if(reference.find("://")!=std::string::npos)throw std::runtime_error("Network textures are not supported");
    // A share on another computer, an absolute path and a path that climbs out of the
    // model's folders are never opened (DependencyScope): only the file name is looked up here.
    auto ref=fs::path(wide(reference));
    if(auto direct=scope.locate(reference);!direct.empty()&&scope.allows(direct))return{direct,false};
    auto name=ref.filename();
    if(name.empty())throw std::runtime_error("Texture reference has no filename");
    // A colocated exact basename is a stronger match than resource aliases.
    if(scope.allows(root/name))return{(root/name).lexically_normal(),true};
    std::vector<fs::path> exact;
    for(size_t i=1;i<folders.size();++i)if(scope.allows(folders[i]/name))exact.push_back((folders[i]/name).lexically_normal());
    std::sort(exact.begin(),exact.end());exact.erase(std::unique(exact.begin(),exact.end()),exact.end());
    if(exact.size()==1)return{exact.front(),true};
    if(exact.size()>1)throw std::runtime_error("Ambiguous texture resource: "+utf8(name.wstring()));
    index();auto found=names.find(key(name.wstring()));
    if(found!=names.end()){
        std::vector<fs::path> usable;for(auto& path:found->second)if(scope.allows(path))usable.push_back(path);
        if(usable.size()==1)return{usable.front(),true};
        if(usable.size()>1)throw std::runtime_error("Ambiguous texture alias: "+utf8(name.wstring()));
    }
    throw std::runtime_error("Missing texture: "+utf8(name.wstring())+" (checked model, tex and textures folders)");
}
}
