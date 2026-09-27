#pragma once
#include "core.hpp"
namespace props {
// Worker import and asynchronous cache loading both use the same material policy.
// Only derived manifest metadata changes; indexed geometry and texture bytes stay intact.
void analyzeMaterials(Asset& asset, const Progress& progress = {});
}
