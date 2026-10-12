#pragma once
// Shared between the ToGL front end files (not part of the game's interface).
#include "togl_abi.hpp"

namespace togl {
// The launcher's GL lookup. GMod's has a leading library name ("libGL.so.1",
// which its launcher ignores) that ToGL 2014's GL_GetProcAddressCallbackFunc_t lacks.
using GLLookup=void*(*)(const char* library,const char* name,bool& okay,bool required,void* fallback);
// GetOpenGLEntryPoints: the real OpenGL table the launcher draws its window with.
void* launcherTable(GLLookup lookup);
// ToGLConnectLibraries: the table shaderapidx9's inlined ToGL code calls. Its
// render-state entry points become Direct3D 9 state (gl_state in togl.cpp).
void* gameTable();
// The fake GL entry point for a function name, or null (gl_tables.cpp traps the rest).
void* fakeGL(const char* name);
void log(const char* format,...) __attribute__((format(printf,1,2)));
// The launcher's ILauncherMgr (SDLMgrInterface001), set by ToGLConnectLibraries.
extern void* launcher;
}
