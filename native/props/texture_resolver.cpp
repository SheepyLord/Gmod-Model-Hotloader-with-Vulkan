#include "texture_resolver.hpp"
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
bool regular(const fs::path& p){std::error_code error;return fs::is_regular_file(p,error);}
}
TextureResolver::TextureResolver(fs::path directory):root(std::move(directory)){
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
    auto ref=fs::path(wide(reference));auto direct=ref.is_absolute()?ref:root/ref;
    if(regular(direct))return{direct.lexically_normal(),false};
    auto name=ref.filename();
    if(name.empty())throw std::runtime_error("Texture reference has no filename");
    // A colocated exact basename is a stronger match than resource aliases.
    if(regular(root/name))return{(root/name).lexically_normal(),true};
    std::vector<fs::path> exact;
    for(size_t i=1;i<folders.size();++i)if(regular(folders[i]/name))exact.push_back((folders[i]/name).lexically_normal());
    std::sort(exact.begin(),exact.end());exact.erase(std::unique(exact.begin(),exact.end()),exact.end());
    if(exact.size()==1)return{exact.front(),true};
    if(exact.size()>1)throw std::runtime_error("Ambiguous texture resource: "+utf8(name.wstring()));
    index();auto found=names.find(key(name.wstring()));
    if(found!=names.end()){
        if(found->second.size()==1)return{found->second.front(),true};
        if(found->second.size()>1)throw std::runtime_error("Ambiguous texture alias: "+utf8(name.wstring()));
    }
    throw std::runtime_error("Missing texture: "+utf8(name.wstring())+" (checked model, tex and textures folders)");
}
}
