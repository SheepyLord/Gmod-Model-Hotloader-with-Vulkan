#pragma once
#include "runtime.hpp"
#include <chrono>
// Imports on a server this game does not host. There every client script comes from
// that server, so a script may start imports of, and read the notes beside, only files
// the player chose in Model Hotloader's own picker; anything else is refused before the
// path is opened, so a refusal says nothing about the disk. Lua can rewrite anything
// under garrysmod/data (sources.local.json, a job's request and status), so the picker's
// answers travel through a private folder (createPrivateFolder) and the files the
// player picked are remembered in %LOCALAPPDATA%\ModelHotloader\picked-models.json.
// Single player and a listen host are unchanged: every client script there is the
// player's own. docs/API.md and docs/FILE_ACCESS.md.
namespace mmd {
class PickedModels {
public:
 // A missing or damaged store means nothing was picked before.
 explicit PickedModels(fs::path store);
 // Chosen in the picker: in this game session (thisSession), or in any session.
 bool picked(std::string_view source,bool thisSession) const;
 // Remembers a file the picker returned. false: it counts, but the store could not be saved.
 bool add(std::string_view source);
private:
 fs::path store;
 std::vector<std::wstring> paths,session;
};
fs::path pickedModelsStore();
// What a script asks to do with a source path: import or reimport it (BeginImport,
// Reload, PropReload: files picked in any session), or read the readmes and licences
// beside it (InspectModelNotes: only files picked in this session).
enum class SourceUse {Import,Notes};
bool sourceAllowed(const PickedModels& picked,std::string_view source,bool localServer,SourceUse use);
// A new folder only this process names, <temp>\<prefix><random hex>, in the user's
// temporary folder unless `temp` is given.
fs::path createPrivateFolder(const std::wstring& prefix,const fs::path& temp={});
// Removes <temp>\<prefix>* folders untouched for `age` (left by a game that crashed); never follows a link.
size_t sweepPrivateFolders(const std::wstring& prefix,std::chrono::hours age,const fs::path& temp={});
}
