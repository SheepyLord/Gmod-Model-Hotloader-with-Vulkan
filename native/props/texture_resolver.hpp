#pragma once
#include "core.hpp"
#include <map>
namespace props {
// The files a model may name (external buffers, material libraries, textures): those
// in its own folder and below it and, for textures, in the tex and textures folders
// the resolver also searches one and two levels up. A model file is untrusted: what
// it reads goes into the cache and to other players with the model, so a reference
// that is absolute, drive- or root-relative or climbs out of these folders is never
// opened (the resolver looks for its file name instead), a link or junction cannot
// lead out of them (the opened file's final path is checked), and nothing the
// process's denylist names (the worker's FilePolicy) is read.
class DependencyScope {
public:
    DependencyScope(fs::path modelFolder,bool textureFolders);
    // The path `reference` (UTF-8, absolute or relative to the model's folder) names,
    // normalized, when that lies in the scope as written; empty otherwise (also for
    // network paths and parts Windows would rewrite, such as a trailing dot).
    fs::path locate(std::string_view reference) const;
    // An existing regular file whose final path, links resolved, lies in the scope and
    // is not denied.
    bool allows(const fs::path& file) const;
    const fs::path& folder() const {return root;}
private:
    fs::path root;
    std::vector<fs::path> trees;                    // as written (lexical)
    mutable std::vector<std::wstring> finals;       // where they really are
    mutable bool resolved=false;
    const std::vector<std::wstring>& finalTrees() const;
};
// Places no model dependency is read from, by final path (the worker sets its FilePolicy).
void setDependencyDenylist(std::function<bool(const fs::path&)> denied);
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
    explicit TextureResolver(fs::path directory);
    ResolvedTexture resolve(const std::string& reference);
};
}
