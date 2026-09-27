#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace props {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr uint32_t Protocol = 2;
struct Limits {
    uint32_t triangleWarning = 1000000, materials = 128, textureDimension = 4096;
    uint64_t packageBytes = 256ull << 20, expandedBytes = 1ull << 30;
};
struct Vec {
    float x{}, y{}, z{};
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(float s) const { return {x*s,y*s,z*s}; }
    float dot(Vec b) const { return x*b.x+y*b.y+z*b.z; }
    Vec cross(Vec b) const { return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec normalized() const { auto l=length(); return l>1e-12f?*this*(1/l):Vec{0,0,1}; }
};
inline void to_json(Json& j,const Vec& v){j=Json::array({v.x,v.y,v.z});}
inline void from_json(const Json& j,Vec& v){if(!j.is_array()||j.size()!=3) throw std::runtime_error("Expected a 3D vector"); v={j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()};}
struct Vertex { Vec pos, normal; float u{},v{}; std::array<float,4> tangent{1,0,0,1}; };
static_assert(sizeof(Vertex)==48);
struct Hull { std::vector<Vec> points; std::vector<uint32_t> indices; };
struct Texture { std::string hash; Bytes png; uint32_t width{},height{}; bool hasAlpha=false, fractionalAlpha=false, alphaKnown=false; };
struct Asset {
    Json manifest;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Hull> hulls;
    std::vector<Texture> textures;
    std::string id;
    // Derived only after validation, never serialized or included in content hashes.
    std::vector<uint32_t> renderIndices;
    Json renderManifest, renderParts, renderChunks;
    Vec renderOrigin;
};
struct Options {
    float scale = 1;
    std::array<float,3> rotation{0,0,0};
    std::string axis = "auto", collision = "hull";
    bool autoUnits = true; // OBJ carries no units: treat very small models as metres
    std::vector<std::string> objects; // .blend: object names to import (empty: every visible mesh)
    Limits limits;
};
struct ProgressUpdate {
    std::string stage; float progress{}; std::string detail;
    uint64_t current{},total{}; bool indeterminate=false;
};
struct Progress {
    std::function<void(const ProgressUpdate&)> report;
    void operator()(std::string stage,float progress,std::string detail={},uint64_t current=0,uint64_t total=0,bool indeterminate=false) const {
        if(report)report({std::move(stage),progress,std::move(detail),current,total,indeterminate});
    }
};
std::wstring wide(std::string_view utf8);
std::string utf8(std::wstring_view text);
Bytes readFile(const fs::path& path,uint64_t maxBytes=256ull<<20);
void writeAtomic(const fs::path& path,std::span<const uint8_t> bytes);
void writeJson(const fs::path& path,const Json& value);
Json readJson(const fs::path& path,uint64_t maxBytes=1ull<<20);
// A bundle's manifest header, read without decoding geometry or textures; null
// when the file is not a readable bundle.
Json bundleManifest(const fs::path& path);
// Removes the named bundles under root (the cache's static folder), their share
// packs and library entries, and the textures no remaining bundle references.
// Returns removedFiles, removedBytes and pendingFiles (files still in use).
Json deleteBundles(const fs::path& root,const std::vector<std::string>& ids);
std::string sha256(std::span<const uint8_t> bytes);
bool validHash(std::string_view s);
void validatePNG(const Texture&);
void validate(const Asset&,const Limits& limits={});
void checkGeometryStorage(uint64_t vertices,uint64_t indices,const Limits& limits={});
void warnLargeGeometry(Json& metadata,uint64_t vertices,uint64_t indices,const Limits& limits={});
Bytes encode(const Asset&);
Asset decode(std::span<const uint8_t>,const Limits& limits={});
std::string saveAsset(const fs::path& cache,Asset&,const Progress& progress={},const Limits& limits={});
Asset loadAsset(const fs::path& cache,const std::string& id,const Limits& limits={});
Json hullJson(const std::vector<Hull>&);
std::vector<Hull> parseHulls(const Json&);
void updateBounds(Asset&);
Options parseOptions(const Json&);
Asset importModel(const fs::path&,const Options&,const Progress&);
Asset importPMX(const fs::path&,const Options&,const Progress&);
Asset importBlend(const fs::path&,const Options&,const Progress&);
// Mesh objects in a .blend file, for choosing which to import.
Json listBlendObjects(const fs::path&,const Options&,const Progress&);
void finishImport(Asset&,const fs::path&,const Options&,const Progress&);
void makeCollision(Asset&,const std::string& mode,const Progress&);
Hull makeFastHull(const Asset&,const Progress& = {},std::string* note=nullptr);
// Validated convex hull of arbitrary points (welds and retries like the fast hull).
Hull hullFromPoints(const std::vector<Vec>& points);
Texture makeTexture(std::span<const uint8_t>,uint32_t limit);
Texture makeMaskedTexture(std::span<const uint8_t>,std::span<const uint8_t>,uint32_t limit,bool white=false);
void applyMaterialOverrides(Asset&,const fs::path&,const Options&);
void addTexture(Asset&,Texture);
struct Hit { float fraction=1; Vec normal{}; bool hit=false; };
Hit trace(const std::vector<Hull>&,Vec start,Vec delta,const std::array<Vec,3>& boxAxes={});
float propScale(double);
void validateHullScale(const std::vector<Hull>&,float);
Hit traceScaled(const std::vector<Hull>&,Vec,Vec,const std::array<Vec,3>&,float);
void prepareRenderPlan(Asset&);
Vertex renderVertex(const Asset&,size_t index,bool backface=false);
}
