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
// player's own. The module's import functions go through prepareImportJob,
// notePickedSource and inspectModelNotesFor, so these rules are tested without a game.
// docs/API.md and docs/FILE_ACCESS.md.
namespace mmd {
class PickedModels {
public:
 // A missing or damaged store means nothing was picked before.
 explicit PickedModels(fs::path store);
 // Chosen in the picker: in this game session (thisSession), or in any session. Picks
 // that another game running at the same time saved since are read on a miss.
 bool picked(std::string_view source,bool thisSession) const;
 // Remembers a file the picker returned, merged into what the store holds now (another
 // game may have saved its own picks). false: it counts, but the store could not be saved.
 bool add(std::string_view source);
private:
 fs::path store;
 std::vector<std::wstring> session;
 mutable std::vector<std::wstring> paths;
 mutable uint64_t seenIndex=0,seenTime=0,seenSize=0;  // the store's file when last read
 void refresh() const;
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
// Where import jobs keep their request, status and log: %TEMP%\mmdhl-jobs (created).
fs::path importJobsFolder();
// The first step of every import job, before the worker starts: on a server this game
// does not host (localServer false) only a source the player picked (refused before
// anything opens the path, with the same sentence whatever is on the disk), never a path
// to another computer; then a new private folder under `jobs` (importJobsFolder() when
// empty) with status.json and, unless the job is the picker, request.json. Returns it.
fs::path prepareImportJob(const PickedModels& picked,bool localServer,bool picker,const std::string& source,const Json& options,const fs::path& jobs={});
// A finished job's result: a picker job that ended "selected" names the file the player
// chose; it is remembered. Any other job's result is not.
void notePickedSource(PickedModels& picked,bool pickerJob,const Json& result);
// InspectModelNotes: the readmes and licences beside a model (model_notes.hpp); on a
// server this game does not host only beside a file picked in this session.
Json inspectModelNotesFor(const PickedModels& picked,bool localServer,const std::string& source);
}
