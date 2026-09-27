#pragma once
#include "core.hpp"
#include <map>
namespace props {
struct ResolvedTexture { fs::path path; bool repaired=false; };
class TextureResolver {
    fs::path root;
    bool indexed=false;
    std::string indexFailure;
    std::vector<fs::path> folders;
    std::map<std::wstring,std::vector<fs::path>> names;
    void index();
public:
    explicit TextureResolver(fs::path directory);
    ResolvedTexture resolve(const std::string& reference);
};
}
