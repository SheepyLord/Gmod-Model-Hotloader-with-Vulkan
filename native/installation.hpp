#pragma once
#include "runtime.hpp"
#include "release.hpp"
namespace mmd {
Json runtimeIdentity();
Json componentIdentity(const char* component, const void* address);
Json workerSelfTest(bool coacd);
Json probeWorker(const fs::path& bin, bool coacd);
}
