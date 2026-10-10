#include "dialogs_posix.hpp"
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string_view>

extern char** environ;

namespace mmd::dialogs {
namespace {
namespace fs=std::filesystem;
// A window answered this soon after it appeared was answered by a key or click meant for
// the game (the Windows dialogs keep their allow buttons disabled as long).
constexpr auto ArmDelay=std::chrono::milliseconds(1200);
struct Program {std::string path;bool zenity=false;};
std::string findProgram(std::string_view name){
 const char* path=getenv("PATH");std::string_view list=path&&*path?path:"/usr/local/bin:/usr/bin:/bin";
 for(size_t start=0;start<=list.size();){auto end=list.find(':',start);if(end==std::string_view::npos)end=list.size();
  auto dir=list.substr(start,end-start);start=end+1;if(dir.empty())continue;
  auto candidate=std::string(dir)+"/"+std::string(name);if(access(candidate.c_str(),X_OK)==0)return candidate;}
 return {};
}
Program program(){
 static const Program found=[]{
  if(auto z=findProgram("zenity");!z.empty())return Program{z,true};
  if(auto k=findProgram("kdialog");!k.empty())return Program{k,false};
  return Program{};
 }();
 if(found.path.empty())throw std::runtime_error("No dialog program found: install zenity (or kdialog) to choose files");
 return found;
}
struct Result {int exitCode=-1;std::string output;};
// Runs the program with these arguments (no shell) and collects its standard output.
Result run(const std::string& exe,const std::vector<std::string>& arguments){
 std::vector<std::string> args{exe};args.insert(args.end(),arguments.begin(),arguments.end());
 std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
 // The Steam overlay library is preloaded into the game; the dialog needs none of it.
 std::vector<std::string> environment;for(char** e=environ;e&&*e;e++){std::string_view v(*e);if(!v.starts_with("LD_PRELOAD="))environment.emplace_back(v);}
 std::vector<char*> envp;for(auto& e:environment)envp.push_back(e.data());envp.push_back(nullptr);
 int out[2];if(pipe2(out,O_CLOEXEC)!=0)throw std::runtime_error("Cannot open the dialog");
 pid_t pid=fork();
 if(pid<0){close(out[0]);close(out[1]);throw std::runtime_error("Cannot open the dialog");}
 if(pid==0){
  int null=open("/dev/null",O_RDWR);if(null>=0){dup2(null,0);dup2(null,2);}
  dup2(out[1],1);
  execve(exe.c_str(),argv.data(),envp.data());_exit(127);
 }
 close(out[1]);Result r;char buffer[4096];
 for(;;){auto n=read(out[0],buffer,sizeof buffer);if(n>0){r.output.append(buffer,size_t(n));if(r.output.size()>(1u<<20))break;continue;}if(n<0&&errno==EINTR)continue;break;}
 close(out[0]);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}
 r.exitCode=WIFEXITED(status)?WEXITSTATUS(status):-1;
 while(!r.output.empty()&&(r.output.back()=='\n'||r.output.back()=='\r'))r.output.pop_back();
 return r;
}
std::vector<std::string> lines(const std::string& text){
 std::vector<std::string> out;for(size_t start=0;start<=text.size();){auto end=text.find('\n',start);if(end==std::string::npos)end=text.size();if(end>start)out.push_back(text.substr(start,end-start));start=end+1;}
 return out;
}
std::string home(){const char* h=getenv("HOME");return h&&*h=='/'?std::string(h)+"/":std::string("/");}
}
std::optional<std::vector<std::string>> pickFiles(const std::string& title,const std::vector<Filter>& filters,bool multiple,bool folder){
 auto p=program();
 for(int attempt=0;;attempt++){
  std::vector<std::string> args;
  if(p.zenity){
   args={"--file-selection","--title="+title,"--filename="+home()};
   if(folder)args.push_back("--directory");
   if(multiple){args.push_back("--multiple");args.push_back("--separator=\n");}
   if(!folder)for(auto& f:filters){std::string spec=f.label+" |";for(auto& pattern:f.patterns)spec+=" "+pattern;args.push_back("--file-filter="+spec);}
  }else{
   args={"--title",title};
   if(folder)args.push_back("--getexistingdirectory");
   else{if(multiple){args.push_back("--multiple");args.push_back("--separate-output");}args.push_back("--getopenfilename");}
   args.push_back(home());
   if(!folder){std::string spec;for(auto& f:filters){std::string patterns;for(auto& pattern:f.patterns)patterns+=(patterns.empty()?"":" ")+pattern;spec+=(spec.empty()?"":"\n")+patterns+"|"+f.label;}args.push_back(spec);}
  }
  auto opened=std::chrono::steady_clock::now();auto r=run(p.path,args);
  if(r.exitCode==127)throw std::runtime_error("The dialog program could not start");
  if(r.exitCode!=0||r.output.empty())return std::nullopt;
  if(std::chrono::steady_clock::now()-opened<ArmDelay&&attempt<3)continue;
  std::vector<std::string> paths;for(auto& line:lines(r.output))if(!line.empty()&&line[0]=='/')paths.push_back(line);
  if(paths.empty())return std::nullopt;
  if(!multiple)paths.resize(1);
  return paths;
 }
}
int choose(const std::string& title,const std::string& text,const std::vector<std::string>& buttons,int defaultButton){
 if(buttons.empty()){inform(title,text);return -1;}
 auto p=program();
 // The default (safe) button answers Enter and a closed window; with zenity it is the
 // cancel button, the first other one OK and a third the extra button.
 std::vector<int> others;for(int i=0;i<int(buttons.size());i++)if(i!=defaultButton)others.push_back(i);
 for(int attempt=0;;attempt++){
  std::vector<std::string> args;
  if(p.zenity){
   args={"--question","--no-markup","--width=560","--title="+title,"--text="+text,"--cancel-label="+buttons[size_t(defaultButton)],"--default-cancel"};
   if(!others.empty())args.push_back("--ok-label="+buttons[size_t(others[0])]);
   if(others.size()>1)args.push_back("--extra-button="+buttons[size_t(others[1])]);
  }else{
   args={"--title",title,others.size()>1?"--warningyesnocancel":"--warningyesno",text,"--yes-label",buttons[size_t(defaultButton)]};
   if(!others.empty()){args.push_back("--no-label");args.push_back(buttons[size_t(others[0])]);}
   if(others.size()>1){args.push_back("--cancel-label");args.push_back(buttons[size_t(others[1])]);}
  }
  auto opened=std::chrono::steady_clock::now();auto r=run(p.path,args);
  int pressed=defaultButton;
  if(p.zenity){if(r.exitCode==0&&!others.empty())pressed=others[0];else if(r.exitCode==1&&others.size()>1&&r.output==buttons[size_t(others[1])])pressed=others[1];}
  else{if(r.exitCode==1&&!others.empty())pressed=others[0];else if(r.exitCode==2&&others.size()>1)pressed=others[1];}
  if(pressed!=defaultButton&&std::chrono::steady_clock::now()-opened<ArmDelay&&attempt<3)continue;
  return pressed;
 }
}
void inform(const std::string& title,const std::string& text){
 auto p=program();
 if(p.zenity)run(p.path,{"--warning","--no-markup","--width=560","--title="+title,"--text="+text});
 else run(p.path,{"--title",title,"--sorry",text});
}
}
