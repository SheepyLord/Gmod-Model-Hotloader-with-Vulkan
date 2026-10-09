#pragma once
#include "runtime.hpp"
#include <exception>
#include <stdexcept>
#include <string_view>
namespace mmd {
// An import failure the player can act on. what() is a complete English sentence
// that names the element at fault (bone 12 "左足", material 7 "スカート"...); code is a
// stable identifier the Lua picks a translated hint with ("pmx.reference",
// "vrm.json", "io.read", "fit.landmarks"...); details carries the same facts as data.
// details["where"] lists the places (see place()) outermost first: the ImportScope
// places around the throw, then the throw site's own.
struct ImportError : std::runtime_error {
    std::string code;
    Json details;
    std::vector<std::string> context;
    ImportError(std::string code,const std::string& message,Json details=Json::object());
};
[[noreturn]] void importFail(std::string code,const std::string& message,Json details=Json::object());
// One step of the place an error names: {"kind":"bone","index":12,"name":"左足"}. The kind
// is an id the Lua translates: vertex, triangle, material, texture, bone, morph,
// display_frame, rigid_body, joint, soft_body, header, text, mesh, primitive, accessor,
// buffer_view, buffer, node, skin, image, humanoid_bone, spring, spring_joint, collider,
// collider_group. A negative index or an empty name is left out; names are kept valid UTF-8.
Json place(std::string_view kind,int64_t index=-1,std::string_view name={});
// The same place in an English message: bone 12 “左足” (a long name is shortened).
std::string placeText(const Json& place);
// 1234567 -> "1,234,567" for messages.
std::string thousands(uint64_t);
// Text from a model file made valid UTF-8 for JSON (invalid bytes become U+FFFD).
std::string cleanText(std::string_view);
// Breadcrumbs for the step being worked on ("Converting VRM avatar", mesh 3 “Body”,
// primitive 2...), one stack per thread. An ImportError records the stack where it is
// created, so the message keeps its place even after the scopes unwind; other
// exceptions get the scopes they unwound through (describeException).
class ImportScope {
public:
    explicit ImportScope(std::string what);
    // domain names the file being read ("vrm"): a JSON error inside becomes vrm.json.
    ImportScope(std::string what,std::string domain);
    // A place (see place()); it joins the "where" of every error raised inside.
    ImportScope(std::string_view kind,int64_t index,std::string_view name);
    ~ImportScope();
    ImportScope(const ImportScope&)=delete;
    ImportScope& operator=(const ImportScope&)=delete;
};
std::vector<std::string> importScopes();
Json importPlaces();
// {error, errorCode, errorDetails, context, exceptionType} for any exception, so the
// worker reports library errors (JSON, filesystem, memory) as readable sentences too. An
// ImportError's error is what() plus its details.why when it has one (fileFailure).
Json describeException(std::exception_ptr);
// The step being worked on as a short code ("parse", "textures"), kept in a fixed
// buffer so a crash handler can read it without allocating.
void setImportStage(const char* code);
const char* importStage();
// What a file is from its first bytes, for one the importer cannot read: id (fbx,
// gltf, glb, dae, obj, blend, x, mqo, 3ds, stl, ply, vmd, vpd, pmm, zip, rar, 7z, gzip,
// zstd, png, jpeg, bmp, gif, webp, dds, psd, pdf, exe, text, pmx, pmd, unknown), an
// English name, and its family: model, convertible, static, archive, motion, image,
// other or unknown. The extension only settles formats without a signature (3DS, TGA).
struct FileFormat {std::string id,name,family;};
FileFormat sniffFormat(std::span<const unsigned char> head,const fs::path& path={});
// The failure for a file that is not a PMX, PMD or VRM character: says what it is
// (an archive, a motion, an image, an FBX file with the wrong extension...).
[[noreturn]] void notCharacterFile(std::span<const unsigned char> head,const fs::path& path);
// A Windows error (GetLastError) in English, and the code the Lua explains it with:
// io.missing, io.denied, io.locked, io.disk_full, io.device or io.read. A failed write is
// io.write (or io.disk_full): the cache, not the model file, is at fault.
std::string systemErrorText(uint32_t error);
std::string systemErrorCode(uint32_t error,bool writing=false);
// A file that cannot be read or written: an ImportError whose code says why, with {path,
// systemError, why} in its details. message stays the short sentence 2.2 had ("Cannot
// read <path>"), since a texture's becomes a manifest warning, part of the asset's
// identity; describeException adds the why to the report.
[[noreturn]] void fileFailure(const std::string& message,const fs::path& path,uint32_t systemError,bool writing=false,Json details=Json::object());
// A worker process that ended without a final status: {exitCode, exitCodeHex, cause,
// error, errorCode}. cause is access_violation, stack_overflow, fail_fast,
// heap_corruption, out_of_memory (errorCode memory), cpp_exception, abort,
// illegal_instruction, divide_by_zero, missing_dll, dll_init, bad_image, breakpoint,
// assertion, killed, no_result or unknown; errorCode is worker.crash otherwise.
Json describeWorkerExit(uint32_t exitCode);
// The result of a worker that has ended. A final status gets exitCode (and, when failed,
// the tail of its crash log as "log"); a status that still says running, or none
// (readError), becomes a failure from describeWorkerExit that keeps the step, file and
// detail it had reached, with the job's source and kind, and this build as its worker
// (the module and the worker ship together).
Json finishedWorkerStatus(Json last,uint32_t exitCode,const std::string& log,const std::string& source,const std::string& kind,const std::string& readError={});
}
