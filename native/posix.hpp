#pragma once
// POSIX counterparts of the Windows services the runtime uses (Linux builds only).
#ifndef _WIN32
#include <sys/types.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mmd::posix {
namespace fs=std::filesystem;
// UTF-8 <-> wchar_t (UTF-32 on Linux). strict: invalid input throws instead of
// becoming U+FFFD, as MB_ERR_INVALID_CHARS / WC_ERR_INVALID_CHARS do.
std::wstring wideFromUtf8(std::string_view text,bool strict=true);
std::string utf8FromWide(std::wstring_view text,bool strict=false);
// UTF-8 <-> UTF-16 (ICU's UChar, Windows' wchar_t): code that must match Windows
// byte for byte (hashes, transliteration) works on UTF-16 everywhere.
std::u16string utf16FromUtf8(std::string_view text,bool strict=true);
std::string utf8FromUtf16(std::u16string_view text);
std::array<unsigned char,32> sha256(std::span<const unsigned char> data);
class Sha256 {
 public:
  Sha256();
  void update(std::span<const unsigned char> data);
  std::array<unsigned char,32> finish();
 private:
  uint32_t state[8];uint64_t length=0;unsigned char block[64];size_t used=0;
  void compress(const unsigned char* data);
};
// Cryptographically random bytes (getrandom); throws when unavailable.
void randomBytes(void* out,size_t size);
// Milliseconds of a monotonic clock (GetTickCount64).
uint64_t tickMs();
// The file of the shared object (or executable) containing address.
fs::path modulePath(const void* address);
fs::path executablePath();
// $TMPDIR, else $XDG_RUNTIME_DIR (when absolute writable directories), else /tmp.
fs::path tempDirectory();
// lstat-based: a directory that is not a symbolic link (Windows: not a reparse point).
bool plainDirectory(const fs::path& path);
bool isSymlink(const fs::path& path);
// Case-insensitive equality of ASCII letters (lstrcmpiW on file extensions and names).
bool equalsIgnoreCase(std::wstring_view a,std::wstring_view b);
// Models made on Windows name their files in any letter case ("Tex/Body.PNG" for
// tex/body.png). The path itself when it exists; otherwise the one existing path whose
// parts match it ignoring ASCII case, part by part from the deepest existing folder; the
// path unchanged when there is none or more than one. Looks only into those folders.
fs::path matchCase(const fs::path& path);
// Replaces to with from (rename). Returns 0 or the errno.
int replaceFile(const fs::path& from,const fs::path& to);
// Moves from to to only when to does not exist (hard link, then unlink). Returns 0 or the errno.
int moveFileNoReplace(const fs::path& from,const fs::path& to);
// A child process in its own process group that dies with the thread that started it
// (the job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE on Windows). stdin is
// /dev/null; stdout/stderr are inherited unless quiet.
struct Child {
  pid_t pid=-1;
  bool finished=false;int exitCode=-1;
  bool running();                 // false once it exited (exitCode set)
  bool wait(int timeoutMs);       // true when it exited within timeoutMs (<0: forever)
  void kill();                    // SIGKILL to the whole group, then reaps
};
Child spawn(const fs::path& executable,const std::vector<std::string>& arguments,const fs::path& directory,bool quiet=true);
// A shared object already loaded into this process, found by file name.
struct Library {
  std::string path;               // as the dynamic loader recorded it
  uintptr_t base=0;               // load bias
  std::vector<std::pair<uintptr_t,uintptr_t>> executable,readonly,all;  // [begin,end) of its PT_LOAD segments
  void* handle=nullptr;           // dlopen(RTLD_NOLOAD) handle, for dlsym
  bool contains(const void* p) const;
  bool executes(const void* p) const;
  void* symbol(const char* name) const;
};
std::optional<Library> loadedLibrary(std::string_view fileName);
// Whether [p, p+size) lies in readable mapped memory of this process.
bool readable(const void* p,size_t size);
// The protection (PROT_* flags) of the mapping holding p, or -1 when unmapped.
int protection(const void* p);
// Sets the protection of the pages holding [p, p+size) (mprotect). Returns false on failure.
bool setProtection(const void* p,size_t size,int prot);
}
#endif
