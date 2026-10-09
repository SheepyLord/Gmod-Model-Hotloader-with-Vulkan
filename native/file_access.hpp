#pragma once
#include "runtime.hpp"
#include <future>
#include <optional>
#include <stdexcept>
// File access for other addons (docs/FILE_ACCESS.md). Lua only asks: the player
// answers in dialogs the worker shows (Lua cannot click another process's windows),
// remembered grants live outside the game folders (Lua can forge anything in data/),
// requests are refused unless this process also runs the server (on a remote server
// every client script comes from that server), and every read reopens the file and
// checks where it really is. Paths never travel back to Lua, only opaque handles.
namespace mmd {
constexpr uint64_t FileReadDefault=1ull<<20,FileReadMaximum=16ull<<20,FileOffsetMaximum=256ull<<20;
constexpr size_t FileListMaximum=4000,FileQueueMaximum=4,FileDenialLimit=3,FileRefusalLimit=10;
// A refusal Lua can name: code is a stable identifier ("network", "not_found",
// "denied_location"...) that the Lua localizes; what() stays an English sentence.
struct FileAccessError : std::runtime_error {
 std::string code;
 FileAccessError(std::string code,const std::string& message);
};
// Places no addon may read even with consent (defense in depth: credential stores,
// browser profiles, keys, Windows, the grants store) and folders too broad to allow
// permanently. Paths are final paths (as resolveFile reports them).
struct FilePolicy {
 std::vector<fs::path> denied;           // folder trees
 std::vector<std::wstring> deniedNames;  // lowercase file name prefixes, anywhere (ntuser.dat)
 std::vector<fs::path> broad;            // exact folders; drive roots are always broad
 // The current user's folders, Steam's config and the game's cfg folder.
 static FilePolicy system(const fs::path& gameRoot);
 bool deniedPath(const fs::path& path) const;
 bool broadFolder(const fs::path& folder) const;
};
// Text from Lua shown in dialogs: valid UTF-8, control, format and bidi characters
// removed, spaces collapsed, at most maxCharacters code points.
std::string cleanLabel(std::string_view text,size_t maxCharacters);
// An absolute X:\ path from Lua checked without touching the disk: no network or
// device forms, no relative, drive-relative, "..", stream or reserved-name parts.
// requireLocal also refuses network drive letters (asked before Windows connects).
fs::path checkRequestedPath(std::string_view utf8,bool requireLocal,const FilePolicy& policy);
// A path below a granted folder ("a/b.txt"): the same rules without a root.
fs::path checkRelativePath(std::string_view utf8);
// What a path really is, opened read-only without following a final link.
struct ResolvedFile {fs::path path;bool folder=false;uint64_t size=0;uint32_t attributes=0;};
ResolvedFile resolveFile(const fs::path& path);
bool insideFolder(const fs::path& path,const fs::path& folder);
// Shown to Lua instead of paths: the user profile folder becomes "~".
std::string displayPath(const fs::path& path);
// Remembered "always allow" answers: per requester label and folder. Labels are what
// a script reports, so this is a convenience, not a boundary between local addons.
struct FileGrant {std::string id,requester;fs::path folder;int64_t created=0,used=0;};
struct FileGrantStore {
 fs::path file;bool enabled=true;std::vector<FileGrant> grants;
 // A missing or unreadable file means no grants; entries that fail the policy are dropped.
 void load(const FilePolicy& policy);
 void save() const;
 const FileGrant* covering(const std::string& requester,const fs::path& path) const;
};
// %LOCALAPPDATA%\ModelHotloader\file-access.json
fs::path fileAccessStore();
struct FileReadResult {std::string data;Json info;};
struct FileAccessConfig {fs::path worker,store,temp;FilePolicy policy;};
// One per client module. Lua calls arrive on the game thread; reads and listings run
// on their own threads with copies of what they need.
class FileAccess {
public:
 explicit FileAccess(FileAccessConfig config);
 ~FileAccess();
 FileAccess(const FileAccess&)=delete;
 FileAccess& operator=(const FileAccess&)=delete;
 Json info();
 uint64_t pick(const Json& options);
 uint64_t request(const Json& options);
 Json poll(uint64_t id);
 uint64_t read(const std::string& handle,const Json& options);
 std::optional<FileReadResult> pollRead(uint64_t id);
 uint64_t list(const std::string& handle,const Json& options);
 std::optional<Json> pollList(uint64_t id);
 void release(const std::string& handle);
 void cancel(uint64_t id);
 Json grants();
 void revoke(const std::string& id);
 // false: off at once. true: {"enabled":true}, or {"request":id} for the confirmation dialog.
 Json setEnabled(bool enabled,const std::string& language);
 struct Item;
 struct Request;
private:
 FileAccessConfig config;
 std::shared_ptr<const FilePolicy> policy;
 FileGrantStore store;
 uint64_t sequence=1,active=0;
 std::map<uint64_t,std::unique_ptr<Request>> requests;
 std::deque<uint64_t> queue;
 std::map<std::string,std::shared_ptr<const Item>> items;
 std::map<std::string,unsigned> denials;
 unsigned refusals=0;
 std::map<uint64_t,std::future<FileReadResult>> reads;
 std::map<uint64_t,std::future<Json>> lists;
 std::vector<std::future<FileReadResult>> droppedReads;  // canceled: a read cannot be interrupted
 std::vector<std::future<Json>> droppedLists;
 size_t running();
 void available(const std::string& requester);
 uint64_t enqueue(std::unique_ptr<Request> r);
 void pump();
 void start(Request& r);
 void finish(Request& r,const Json& answer);
 Json grant(Request& r,const ResolvedFile& target,const std::string& grantId);
 void stop(Request& r);
 std::shared_ptr<const Item> item(const std::string& handle) const;
};
// Worker side (mmdhl_worker --fa-pick|--fa-consent <private folder>): shows the dialog
// that request.json describes and writes result.json. Dialog text is compiled in.
int fileAccessDialog(bool pick,const fs::path& folder);
}
