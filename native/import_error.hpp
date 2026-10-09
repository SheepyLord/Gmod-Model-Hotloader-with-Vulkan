#pragma once
#include "runtime.hpp"
#include <exception>
#include <stdexcept>
namespace mmd {
// An import failure the player can act on. what() is a complete English sentence
// that names the element at fault (bone 12 "左足", material 7 "スカート"...); code is a
// stable identifier the Lua picks a translated hint with ("pmx.reference",
// "vrm.json", "io.read", "fit.landmarks"...); details carries the same facts as data.
struct ImportError : std::runtime_error {
    std::string code;
    Json details;
    std::vector<std::string> context;
    ImportError(std::string code,const std::string& message,Json details=Json::object());
};
[[noreturn]] void importFail(std::string code,const std::string& message,Json details=Json::object());
// Breadcrumbs for the step being worked on ("Converting VRM avatar", "mesh \"Body\"
// primitive 2"...), one stack per thread. An ImportError records the stack where it is
// created, so the message keeps its place even after the scopes unwind.
class ImportScope {
public:
    explicit ImportScope(std::string what);
    ~ImportScope();
    ImportScope(const ImportScope&)=delete;
    ImportScope& operator=(const ImportScope&)=delete;
};
std::vector<std::string> importScopes();
// {error, errorCode, errorDetails, context, exceptionType} for any exception, so the
// worker reports library errors (JSON, filesystem, memory) as readable sentences too.
Json describeException(std::exception_ptr);
}
