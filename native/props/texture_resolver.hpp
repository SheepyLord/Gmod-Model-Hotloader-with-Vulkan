#pragma once
#include "core.hpp"
#include "../dependency_scope.hpp"
#include <map>
namespace props {
// The files a model may name: its own folders only (dependency_scope.hpp, in the runtime
// so that every importer shares the worker's denylist).
using mmd::DependencyScope;
using mmd::setDependencyDenylist;
struct ResolvedTexture { fs::path path; bool repaired=false; };
class TextureResolver {
    fs::path root;
    DependencyScope scope;
    bool indexed=false;
    std::string indexFailure;
    std::vector<fs::path> folders;
    std::map<std::wstring,std::vector<fs::path>> names;
    void index();
public:
    // Static props keep 2.2's reach (DependencyScope::Reach::Local); characters in other
    // formats pass Reach::Confined.
    explicit TextureResolver(fs::path directory,DependencyScope::Reach reach=DependencyScope::Reach::Local);
    ResolvedTexture resolve(const std::string& reference);
    // The reference names a file the model may not read as written: a denied place or a
    // network path (for a confined resolver also one outside the model's folders or
    // through a link out of them). resolve()
    // then looks for its file name instead.
    bool refuses(const std::string& reference) const;
};
}
