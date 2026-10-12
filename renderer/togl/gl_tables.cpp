// The two COpenGLEntryPoints tables (GMod's layout, gl_layout.inc).
//  - The launcher draws its window with real OpenGL: GetOpenGLEntryPoints fills
//    the table as ToGL's constructor does (glentrypoints.cpp, MIT).
//  - shaderapidx9 gets its table from ToGLConnectLibraries. ToGL's inline state
//    code calls it for every render state; those entries are togl.cpp's
//    translators to Direct3D 9. Any other entry is a trap that logs its name
//    once: nothing in the game should reach real OpenGL through it.
#include "togl_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>
#include <utility>

namespace togl {
namespace {
constexpr size_t TableSize=0x718;
// Header of GMod's COpenGLEntryPoints (one field more than ToGL 2014's).
struct TableHeader {
 uint64_t totalGLCycles,totalGLCalls;
 const char* library;  // GMod's addition: "libGL.so.1", the lookup's first argument
 int versionMajor,versionMinor,versionPatch;
 bool haveOpenGL;
 char* driverStrings[4];  // vendor, renderer, version, extensions
 int driverProvider;
};
static_assert(offsetof(TableHeader,versionMajor)==24&&offsetof(TableHeader,haveOpenGL)==36&&offsetof(TableHeader,driverStrings)==40&&offsetof(TableHeader,driverProvider)==72);
constexpr int HaveOpenGLOffset=36;

struct Entry { int offset; const char* name; bool flag; int major,minor; const char* ext; bool required; };
constexpr Entry entries[]={
#define GL_FLAG(offset,name,major,minor) {offset,#name,true,major,minor,nullptr,false},
#define GL_SLOT(offset,name,ext,required) {offset,#name,false,0,0,#ext,required},
#include "gl_layout.inc"
#undef GL_FLAG
#undef GL_SLOT
};
constexpr size_t EntryCount=sizeof(entries)/sizeof(entries[0]);
static_assert(entries[EntryCount-1].offset<int(TableSize));

int flagOffset(const char* ext) {
 if(!strcmp(ext,"OpenGL"))return HaveOpenGLOffset;
 for(auto& e:entries){if(e.flag&&!strcmp(e.name,ext))return e.offset;}
 return -1;
}
int slotOffset(const char* name) {
 for(auto& e:entries){if(!e.flag&&!strcmp(e.name,name))return e.offset;}
 return -1;
}

alignas(16) uint8_t realTable[TableSize];
alignas(16) uint8_t fakeTable[TableSize];
bool realBuilt=false,fakeBuilt=false;

template<class T> T& at(uint8_t* table,int offset) { return *reinterpret_cast<T*>(table+offset); }

void versionOf(const char* version,int& major,int& minor,int& patch) {
 major=minor=patch=0;if(version)sscanf(version,"%d.%d.%d",&major,&minor,&patch);
}
bool hasExtension(const char* list,const char* ext) {
 if(!list)return false;
 size_t n=strlen(ext);
 for(const char* p=list;(p=strstr(p,ext));p+=n)if((p==list||p[-1]==' ')&&(p[n]==' '||p[n]==0))return true;
 return false;
}

// Trap entries: one function per slot, so the log names the entry.
std::atomic<bool> trapped[EntryCount];
void reportTrap(size_t index) {
 if(!trapped[index].exchange(true))log("unexpected OpenGL call through the game's ToGL table: %s (ignored)",entries[index].name);
}
template<size_t I> uint64_t trap() { reportTrap(I); return 0; }
template<size_t... I> constexpr std::array<void*,sizeof...(I)> makeTraps(std::index_sequence<I...>) { return {reinterpret_cast<void*>(&trap<I>)...}; }
const auto traps=makeTraps(std::make_index_sequence<EntryCount>{});
}

void* launcherTable(GLLookup lookup) {
 if(realBuilt)return realTable;
 memset(realTable,0,sizeof(realTable));
 auto& h=*reinterpret_cast<TableHeader*>(realTable);
 h.library="libGL.so.1";
 bool ok=true;
 auto get=[&](const char* name,bool& okay,bool required){return lookup(h.library,name,okay,required,nullptr);};
 auto getString=reinterpret_cast<const char*(*)(GLenum)>(get("glGetString",ok,true));
 auto glxDisplay=reinterpret_cast<void*(*)()>(get("glXGetCurrentDisplay",ok=true,false));
 auto glxExtensions=reinterpret_cast<const char*(*)(void*,int)>(get("glXQueryExtensionsString",ok=true,false));
 const char* version=getString?getString(0x1F02):nullptr;     // GL_VERSION
 const char* extensions=getString?getString(0x1F03):nullptr;  // GL_EXTENSIONS
 const char* glx=glxDisplay&&glxExtensions&&glxDisplay()?glxExtensions(glxDisplay(),0):nullptr;
 versionOf(version,h.versionMajor,h.versionMinor,h.versionPatch);
 int have=h.versionMajor*100000+h.versionMinor*1000+h.versionPatch;
 h.haveOpenGL=have>=200000;
 for(auto& e:entries){
  if(e.flag){
   bool core=e.major>=0&&e.minor>=0&&have>=e.major*100000+e.minor*1000;
   at<bool>(realTable,e.offset)=core||hasExtension(extensions,e.name)||hasExtension(glx,e.name);
  }else{
   int flag=flagOffset(e.ext);
   bool okay=flag>=0?at<bool>(realTable,flag):false;
   at<void*>(realTable,e.offset)=get(e.name,okay,e.required);
   if(flag>=0)at<bool>(realTable,flag)=okay;
  }
 }
 // As ToGL: one extension under two names, and the ARB framebuffer functions behind the EXT names.
 bool bgra=at<bool>(realTable,flagOffset("GL_EXT_vertex_array_bgra"))||at<bool>(realTable,flagOffset("GL_ARB_vertex_array_bgra"));
 at<bool>(realTable,flagOffset("GL_EXT_vertex_array_bgra"))=at<bool>(realTable,flagOffset("GL_ARB_vertex_array_bgra"))=bgra;
 if(at<bool>(realTable,flagOffset("GL_ARB_framebuffer_object"))){
  for(auto name:{"GL_EXT_framebuffer_object","GL_EXT_framebuffer_blit","GL_EXT_framebuffer_multisample"})at<bool>(realTable,flagOffset(name))=true;
  static const char* pairs[][2]={{"glBindFramebufferEXT","glBindFramebuffer"},{"glBindRenderbufferEXT","glBindRenderbuffer"},{"glCheckFramebufferStatusEXT","glCheckFramebufferStatus"},
   {"glDeleteRenderbuffersEXT","glDeleteRenderbuffers"},{"glFramebufferRenderbufferEXT","glFramebufferRenderbuffer"},{"glFramebufferTexture2DEXT","glFramebufferTexture2D"},
   {"glFramebufferTexture3DEXT","glFramebufferTexture3D"},{"glGenFramebuffersEXT","glGenFramebuffers"},{"glGenRenderbuffersEXT","glGenRenderbuffers"},
   {"glDeleteFramebuffersEXT","glDeleteFramebuffers"},{"glBlitFramebufferEXT","glBlitFramebuffer"},{"glRenderbufferStorageMultisampleEXT","glRenderbufferStorageMultisample"}};
  for(auto& p:pairs){int to=slotOffset(p[0]),from=slotOffset(p[1]);if(to>=0&&from>=0)at<void*>(realTable,to)=at<void*>(realTable,from);}
 }
 const char* strings[4]={getString?getString(0x1F00):nullptr,getString?getString(0x1F01):nullptr,version,extensions};
 for(int i=0;i<4;i++)h.driverStrings[i]=strdup(strings[i]?strings[i]:"");
 const char* vendor=h.driverStrings[0];
 h.driverProvider=strcasestr(vendor,"nvidia")?1:(strcasestr(vendor,"amd")||strcasestr(vendor,"ati"))?2:strcasestr(vendor,"intel")?4:0;
 realBuilt=true;
 return realTable;
}

void* gameTable() {
 if(fakeBuilt)return fakeTable;
 // The header and extension flags as the launcher's table has them (the game may read them).
 if(realBuilt)memcpy(fakeTable,realTable,sizeof(fakeTable));else memset(fakeTable,0,sizeof(fakeTable));
 for(size_t i=0;i<EntryCount;i++){
  auto& e=entries[i];if(e.flag)continue;
  void* fake=fakeGL(e.name);
  at<void*>(fakeTable,e.offset)=fake?fake:traps[i];
 }
 // SetTexture's inline code then makes one call (glBindMultiTextureEXT) per binding.
 at<bool>(fakeTable,flagOffset("GL_EXT_direct_state_access"))=true;
 fakeBuilt=true;
 return fakeTable;
}
}
