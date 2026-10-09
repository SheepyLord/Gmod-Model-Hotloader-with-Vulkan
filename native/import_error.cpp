#include "import_error.hpp"
#include <new>
#include <system_error>
namespace mmd {
namespace {
struct Scope { std::string what; int unwinding; };
thread_local std::vector<Scope> scopes;
// Scopes left by an exception that is not an ImportError, innermost last, so the
// report still says where a library error (JSON, filesystem) happened.
thread_local std::vector<std::string> unwound;
std::vector<std::string> names(){std::vector<std::string> r;for(auto& s:scopes)r.push_back(s.what);return r;}
}
ImportError::ImportError(std::string c,const std::string& message,Json d):std::runtime_error(message),code(std::move(c)),details(std::move(d)),context(names()){
    if(!details.is_object())details=Json::object();
}
void importFail(std::string code,const std::string& message,Json details){throw ImportError(std::move(code),message,std::move(details));}
ImportScope::ImportScope(std::string what){if(scopes.empty())unwound.clear();scopes.push_back({std::move(what),std::uncaught_exceptions()});}
ImportScope::~ImportScope(){
    if(scopes.empty())return;
    if(std::uncaught_exceptions()>scopes.back().unwinding)unwound.insert(unwound.begin(),scopes.back().what);
    scopes.pop_back();
}
std::vector<std::string> importScopes(){return names();}
Json describeException(std::exception_ptr error){
    Json r={{"error","Unknown error"},{"errorCode","unknown"},{"errorDetails",Json::object()},{"context",Json::array()},{"exceptionType","unknown"}};
    auto context=[&](const std::vector<std::string>& c){r["context"]=c;};
    try{if(error)std::rethrow_exception(error);}
    catch(const ImportError& e){r["error"]=e.what();r["errorCode"]=e.code;r["errorDetails"]=e.details;context(e.context);r["exceptionType"]="import";return r;}
    catch(const Json::exception& e){
        r["error"]=std::string("The file's JSON data is malformed or incomplete: ")+e.what();r["errorCode"]="json";r["exceptionType"]="json";
        r["errorDetails"]={{"id",e.id}};
    }
    catch(const fs::filesystem_error& e){
        bool full=e.code()==std::errc::no_space_on_device||e.code().value()==112/*ERROR_DISK_FULL*/;
        std::string path=e.path1().empty()?std::string():utf8(e.path1().wstring());
        r["error"]=full?"The disk is full: "+path:"Cannot access "+(path.empty()?std::string("a file"):path)+": "+e.code().message();
        r["errorCode"]=full?"io.disk_full":"io.filesystem";r["exceptionType"]="filesystem";
        r["errorDetails"]={{"path",path},{"systemError",e.code().value()},{"systemMessage",e.code().message()}};
    }
    catch(const std::bad_alloc&){r["error"]="The importer ran out of memory";r["errorCode"]="memory";r["exceptionType"]="memory";}
    catch(const std::exception& e){r["error"]=e.what();r["exceptionType"]="std";}
    catch(...){}
    context(unwound);
    return r;
}
}
