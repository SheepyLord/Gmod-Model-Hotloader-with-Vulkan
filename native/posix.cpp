// POSIX counterparts of the Windows services the runtime uses (Linux builds only).
#include "posix.hpp"
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

extern char** environ;

namespace mmd::posix {
namespace {
// Decodes one UTF-8 sequence at text[at]; returns the code point or -1 (invalid).
long decodeUtf8(std::string_view text,size_t& at){
 unsigned c=static_cast<unsigned char>(text[at]);
 if(c<0x80){at++;return long(c);}
 int extra=c>=0xc2&&c<0xe0?1:c>=0xe0&&c<0xf0?2:c>=0xf0&&c<0xf5?3:-1;
 if(extra<0||at+size_t(extra)>=text.size())return -1;
 unsigned long value=c&(0x3fu>>extra);
 for(int k=1;k<=extra;k++){unsigned b=static_cast<unsigned char>(text[at+size_t(k)]);if((b&0xc0)!=0x80)return -1;value=value<<6|(b&0x3f);}
 static const unsigned long minimum[]={0,0x80,0x800,0x10000};
 if(value<minimum[extra]||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return -1;
 at+=size_t(extra)+1;return long(value);
}
void appendUtf8(std::string& out,unsigned long c){
 if(c<0x80)out+=char(c);
 else if(c<0x800){out+=char(0xc0|c>>6);out+=char(0x80|(c&0x3f));}
 else if(c<0x10000){out+=char(0xe0|c>>12);out+=char(0x80|(c>>6&0x3f));out+=char(0x80|(c&0x3f));}
 else{out+=char(0xf0|c>>18);out+=char(0x80|(c>>12&0x3f));out+=char(0x80|(c>>6&0x3f));out+=char(0x80|(c&0x3f));}
}
}
std::wstring wideFromUtf8(std::string_view text,bool strict){
 std::wstring out;out.reserve(text.size());
 for(size_t at=0;at<text.size();){auto c=decodeUtf8(text,at);if(c<0){if(strict)throw std::runtime_error("Invalid UTF-8 path");c=0xfffd;at++;}out+=wchar_t(c);}
 return out;
}
std::string utf8FromWide(std::wstring_view text,bool strict){
 std::string out;out.reserve(text.size());
 for(wchar_t w:text){auto c=static_cast<unsigned long>(static_cast<uint32_t>(w));if(c>0x10ffff||(c>=0xd800&&c<=0xdfff)){if(strict)throw std::runtime_error("Invalid Unicode string");c=0xfffd;}appendUtf8(out,c);}
 return out;
}
std::u16string utf16FromUtf8(std::string_view text,bool strict){
 std::u16string out;out.reserve(text.size());
 for(size_t at=0;at<text.size();){auto c=decodeUtf8(text,at);if(c<0){if(strict)throw std::runtime_error("Invalid UTF-8 text");c=0xfffd;at++;}
  if(c>=0x10000){c-=0x10000;out+=char16_t(0xd800+(c>>10));out+=char16_t(0xdc00+(c&0x3ff));}else out+=char16_t(c);}
 return out;
}
std::string utf8FromUtf16(std::u16string_view text){
 std::string out;out.reserve(text.size());
 for(size_t i=0;i<text.size();i++){unsigned long c=text[i];
  if(c>=0xd800&&c<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff){c=0x10000+((c-0xd800)<<10)+(text[i+1]-0xdc00);i++;}
  else if(c>=0xd800&&c<=0xdfff)c=0xfffd;
  appendUtf8(out,c);}
 return out;
}
// FIPS 180-4 SHA-256.
namespace {
constexpr uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
inline uint32_t rotr(uint32_t x,int n){return x>>n|x<<(32-n);}
}
Sha256::Sha256():state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}{}
void Sha256::compress(const unsigned char* p){
 uint32_t w[64];
 for(int i=0;i<16;i++)w[i]=uint32_t(p[i*4])<<24|uint32_t(p[i*4+1])<<16|uint32_t(p[i*4+2])<<8|uint32_t(p[i*4+3]);
 for(int i=16;i<64;i++){uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
 uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
 for(int i=0;i<64;i++){uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^(~e&g),t1=h+S1+ch+K[i]+w[i],S0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c),t2=S0+maj;
  h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
void Sha256::update(std::span<const unsigned char> data){
 length+=data.size();size_t at=0;
 if(used){size_t take=std::min(data.size(),64-used);std::memcpy(block+used,data.data(),take);used+=take;at=take;if(used<64)return;compress(block);used=0;}
 for(;at+64<=data.size();at+=64)compress(data.data()+at);
 if(at<data.size()){used=data.size()-at;std::memcpy(block,data.data()+at,used);}
}
std::array<unsigned char,32> Sha256::finish(){
 uint64_t bits=length*8;unsigned char pad=0x80;update({&pad,1});unsigned char zero=0;while(used!=56)update({&zero,1});
 unsigned char tail[8];for(int i=0;i<8;i++)tail[i]=static_cast<unsigned char>(bits>>(56-8*i));update({tail,8});
 std::array<unsigned char,32> digest{};for(int i=0;i<8;i++)for(int k=0;k<4;k++)digest[i*4+k]=static_cast<unsigned char>(state[i]>>(24-8*k));
 return digest;
}
std::array<unsigned char,32> sha256(std::span<const unsigned char> data){Sha256 s;s.update(data);return s.finish();}
void randomBytes(void* out,size_t size){
 auto p=static_cast<unsigned char*>(out);
 while(size){auto n=getrandom(p,size,0);if(n<0){if(errno==EINTR)continue;throw std::runtime_error("Secure random numbers are unavailable");}p+=n;size-=size_t(n);}
}
uint64_t tickMs(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000+uint64_t(t.tv_nsec)/1000000;}
fs::path modulePath(const void* address){
 Dl_info info{};if(!dladdr(address,&info)||!info.dli_fname||!*info.dli_fname)throw std::runtime_error("Cannot locate loaded native component");
 std::error_code error;auto path=fs::path(info.dli_fname);
 // The main executable is reported by the name it was started with.
 if(path.is_relative()||!fs::exists(path,error))return executablePath();
 auto resolved=fs::canonical(path,error);return error?path:resolved;
}
fs::path executablePath(){std::error_code error;auto path=fs::read_symlink("/proc/self/exe",error);if(error)throw std::runtime_error("Cannot locate the running executable");return path;}
fs::path tempDirectory(){
 // The user's own runtime folder (0700) first: /tmp is shared with every account.
 for(auto variable:{"TMPDIR","XDG_RUNTIME_DIR"})if(auto value=getenv(variable);value&&*value){
  std::error_code error;fs::path p(value);if(p.is_absolute()&&fs::is_directory(p,error)&&access(p.c_str(),W_OK|X_OK)==0)return p;}
 return "/tmp";
}
bool plainDirectory(const fs::path& path){struct stat s{};return lstat(path.c_str(),&s)==0&&S_ISDIR(s.st_mode);}
bool isSymlink(const fs::path& path){struct stat s{};return lstat(path.c_str(),&s)==0&&S_ISLNK(s.st_mode);}
bool equalsIgnoreCase(std::wstring_view a,std::wstring_view b){
 if(a.size()!=b.size())return false;
 for(size_t i=0;i<a.size();i++){auto x=a[i],y=b[i];if(x>=L'A'&&x<=L'Z')x+=32;if(y>=L'A'&&y<=L'Z')y+=32;if(x!=y)return false;}
 return true;
}
fs::path matchCase(const fs::path& path){
 struct stat st{};
 if(path.empty()||lstat(path.c_str(),&st)==0)return path;
 std::vector<fs::path> missing;fs::path base=path;
 while(base.has_relative_path()&&lstat(base.c_str(),&st)!=0){missing.push_back(base.filename());base=base.parent_path();}
 if(base.empty())base=".";
 for(auto part=missing.rbegin();part!=missing.rend();++part){
  auto want=part->wstring();if(want.empty()||want==L"."||want==L"..")return path;
  fs::path found;size_t seen=0;std::error_code error;
  for(fs::directory_iterator it(base,fs::directory_options::skip_permission_denied,error),end;!error&&it!=end;it.increment(error)){
   if(++seen>8192)return path;
   if(equalsIgnoreCase(it->path().filename().wstring(),want)){if(!found.empty())return path;found=it->path();}
  }
  if(found.empty())return path;
  base=found;
 }
 return base;
}
int replaceFile(const fs::path& from,const fs::path& to){return rename(from.c_str(),to.c_str())==0?0:errno;}
int moveFileNoReplace(const fs::path& from,const fs::path& to){
 if(link(from.c_str(),to.c_str())!=0){
  int error=errno;
  // Filesystems without hard links: renameat2(RENAME_NOREPLACE) is not everywhere either;
  // only rename when the target is certainly absent.
  if(error==EPERM||error==ENOTSUP||error==EOPNOTSUPP){struct stat s{};if(lstat(to.c_str(),&s)==0)return EEXIST;return rename(from.c_str(),to.c_str())==0?0:errno;}
  return error;
 }
 unlink(from.c_str());return 0;
}
bool Child::running(){
 if(finished||pid<=0)return false;
 int status=0;auto r=waitpid(pid,&status,WNOHANG);
 if(r==0)return true;
 finished=true;exitCode=r==pid?(WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0)):-1;
 return false;
}
bool Child::wait(int timeoutMs){
 auto start=tickMs();
 while(running()){if(timeoutMs>=0&&tickMs()-start>=uint64_t(timeoutMs))return false;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 return true;
}
void Child::kill(){
 if(pid<=0||finished)return;
 ::kill(-pid,SIGKILL);::kill(pid,SIGKILL);
 int status=0;if(waitpid(pid,&status,0)==pid){finished=true;exitCode=128+SIGKILL;}
}
Child spawn(const fs::path& executable,const std::vector<std::string>& arguments,const fs::path& directory,bool quiet){
 // Zip archives (GitHub's artifacts among them) often lose the executable bit: give it back
 // to a program that is readable but not executable, as the user can read it.
 {struct stat st{};if(stat(executable.c_str(),&st)==0&&S_ISREG(st.st_mode)&&access(executable.c_str(),X_OK)!=0){
   mode_t mode=st.st_mode&07777;if(mode&S_IRUSR)mode|=S_IXUSR;if(mode&S_IRGRP)mode|=S_IXGRP;if(mode&S_IROTH)mode|=S_IXOTH;chmod(executable.c_str(),mode);}}
 std::vector<std::string> args{executable.string()};args.insert(args.end(),arguments.begin(),arguments.end());
 std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
 // The Steam overlay library is preloaded into the game; the worker needs none of it.
 std::vector<std::string> environment;for(char** e=environ;e&&*e;e++){std::string_view v(*e);if(!v.starts_with("LD_PRELOAD="))environment.emplace_back(v);}
 std::vector<char*> envp;for(auto& e:environment)envp.push_back(e.data());envp.push_back(nullptr);
 auto exe=executable.string(),dir=directory.string();
 const pid_t parent=getpid();
 pid_t pid=fork();
 if(pid<0)throw std::runtime_error(std::string("Cannot start process: ")+std::strerror(errno));
 if(pid==0){
  // Only async-signal-safe calls until exec.
  setpgid(0,0);
  prctl(PR_SET_PDEATHSIG,SIGKILL);
  if(getppid()!=parent)_exit(127);
  int null=open("/dev/null",O_RDWR);
  if(null>=0){dup2(null,0);if(quiet){dup2(null,1);dup2(null,2);}if(null>2)close(null);}
  if(!dir.empty()&&chdir(dir.c_str())!=0)_exit(127);
  execve(exe.c_str(),argv.data(),envp.data());
  _exit(127);
 }
 setpgid(pid,pid);
 Child child;child.pid=pid;return child;
}
bool Library::contains(const void* p) const{auto a=reinterpret_cast<uintptr_t>(p);for(auto [b,e]:all)if(a>=b&&a<e)return true;return false;}
bool Library::executes(const void* p) const{auto a=reinterpret_cast<uintptr_t>(p);for(auto [b,e]:executable)if(a>=b&&a<e)return true;return false;}
void* Library::symbol(const char* name) const{return handle?dlsym(handle,name):nullptr;}
std::optional<Library> loadedLibrary(std::string_view fileName){
 struct Search {std::string_view name;std::optional<Library> found;} search{fileName,std::nullopt};
 dl_iterate_phdr([](dl_phdr_info* info,size_t,void* data)->int{
  auto& s=*static_cast<Search*>(data);if(!info->dlpi_name||!*info->dlpi_name)return 0;
  std::string_view path(info->dlpi_name);auto slash=path.rfind('/');auto name=slash==std::string_view::npos?path:path.substr(slash+1);
  if(name!=s.name)return 0;
  Library lib;lib.path=std::string(path);lib.base=info->dlpi_addr;
  for(int i=0;i<info->dlpi_phnum;i++){const auto& ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD)continue;
   uintptr_t begin=info->dlpi_addr+ph.p_vaddr,end=begin+ph.p_memsz;lib.all.emplace_back(begin,end);
   if(ph.p_flags&PF_X)lib.executable.emplace_back(begin,end);else if(!(ph.p_flags&PF_W))lib.readonly.emplace_back(begin,end);}
  s.found=std::move(lib);return 1;
 },&search);
 if(search.found){search.found->handle=dlopen(search.found->path.c_str(),RTLD_NOW|RTLD_NOLOAD);
  // The handle only names an object that stays loaded with the game; keep the reference count as it was.
  if(search.found->handle)dlclose(search.found->handle);}
 return search.found;
}
namespace {
struct Region {uintptr_t begin,end;bool read,write,exec;};
std::mutex mapsMutex;std::vector<Region> regions;
void loadMaps(){
 regions.clear();std::ifstream maps("/proc/self/maps");std::string line;
 while(std::getline(maps,line)){unsigned long long b=0,e=0;char perms[8]{};if(sscanf(line.c_str(),"%llx-%llx %7s",&b,&e,perms)!=3)continue;regions.push_back({uintptr_t(b),uintptr_t(e),perms[0]=='r',perms[1]=='w',perms[2]=='x'});}
}
bool covered(uintptr_t a,uintptr_t end){
 for(auto it=std::upper_bound(regions.begin(),regions.end(),a,[](uintptr_t v,const Region& r){return v<r.begin;});it!=regions.begin();){
  --it;if(a<it->begin||a>=it->end||!it->read)return false;if(end<=it->end)return true;a=it->end;
  // Contiguous mappings continue the range.
  auto next=it+1;if(next==regions.end()||next->begin!=a)return false;it=next+1;
 }
 return false;
}
}
bool readable(const void* p,size_t size){
 if(!p)return false;auto a=reinterpret_cast<uintptr_t>(p);if(a+size<a)return false;
 std::lock_guard lock(mapsMutex);
 if(regions.empty())loadMaps();
 if(covered(a,a+size))return true;
 loadMaps();return covered(a,a+size);
}
int protection(const void* p){
 auto a=reinterpret_cast<uintptr_t>(p);std::lock_guard lock(mapsMutex);loadMaps();
 for(auto& r:regions)if(a>=r.begin&&a<r.end)return (r.read?PROT_READ:0)|(r.write?PROT_WRITE:0)|(r.exec?PROT_EXEC:0);
 return -1;
}
bool setProtection(const void* p,size_t size,int prot){
 auto page=uintptr_t(sysconf(_SC_PAGESIZE));auto a=reinterpret_cast<uintptr_t>(p);auto begin=a&~(page-1),end=(a+size+page-1)&~(page-1);
 bool ok=mprotect(reinterpret_cast<void*>(begin),end-begin,prot)==0;
 std::lock_guard lock(mapsMutex);regions.clear();return ok;
}
}
