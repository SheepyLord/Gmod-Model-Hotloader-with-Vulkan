#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
// The files a model may name (external buffers, material libraries, textures): those
// in its own folder and below it and, for textures, in the tex and textures folders
// the static prop resolver also searches one and two levels up. A model file is
// untrusted: what it reads goes into the cache and to other players with the model, so
// a reference that is absolute, drive- or root-relative or climbs out of these folders
// is never opened, a link or junction cannot lead out of them, and nothing the
// process's denylist names (the worker's FilePolicy) is read. Every importer uses it:
// PMX and PMD characters (assets.cpp), FBX, glTF and DAE characters
// (character_import.cpp) and static props (props/import.cpp, props/texture_resolver.cpp).
// It lives in the runtime so that one denylist serves them all.
namespace mmd {
class DependencyScope {
public:
    using path = std::filesystem::path;
    DependencyScope(path modelFolder,bool textureFolders);
    // The path `reference` (UTF-8, absolute or relative to the model's folder) names,
    // normalized, when that lies in the scope as written; empty otherwise (also for
    // network paths and parts Windows would rewrite, such as a trailing dot).
    path locate(std::string_view reference) const;
    // An existing regular file that really lies in the scope and is not denied. Where it
    // really is: the opened file's final path, links resolved, or, where Windows cannot
    // say (some RAM disks, FUSE and cloud drives), a path as written with no link or
    // junction between the scope's folder and the file.
    bool allows(const path& file) const;
    const path& folder() const {return root;}
private:
    path root;
    std::vector<path> trees;                        // as written (lexical)
    mutable std::vector<std::wstring> finals;       // where they really are
    mutable bool resolved=false;
    const std::vector<std::wstring>& finalTrees() const;
};
// Places no model dependency is read from, by final path (the worker sets its FilePolicy).
void setDependencyDenylist(std::function<bool(const std::filesystem::path&)> denied);
// Tests only: act as if Windows could not say where any open file really is.
void simulateUnknownFinalPaths(bool unknown);
}
