#include "texture_resolver.hpp"
void textureResolverTests(){
    const auto dir=fs::temp_directory_path()/fs::path(L"gmodel-resolver-test-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(dir/L"model"/L"tex");fs::create_directories(dir/L"textures");
    const Bytes data{1,2,3};
    writeAtomic(dir/L"model"/L"body.png",data);
    writeAtomic(dir/L"model"/L"tex"/L"normal_map.png",data);
    writeAtomic(dir/L"textures"/L"中文.png",data);
    TextureResolver resolver(dir/L"model");
    check(!resolver.resolve("body.png").repaired,"exact texture reference changed");
    check(resolver.resolve(utf8((dir/L"old-artist-folder"/L"body.png").wstring())).path==dir/L"model"/L"body.png","stale absolute reference did not use local basename");
    check(resolver.resolve("normal map.png").path==dir/L"model"/L"tex"/L"normal_map.png","texture space/underscore alias did not resolve");
    check(resolver.resolve(utf8(L"中文.png")).path==dir/L"textures"/L"中文.png","Unicode sibling texture did not resolve");
    rejects([&]{resolver.resolve("different-image.png");},"unrelated texture name was guessed");
    rejects([&]{resolver.resolve("https://example.invalid/body.png");},"network texture reference accepted");
    // Shares on other computers, in every spelling Windows accepts, are never opened.
    for(auto unc:{"\\\\example.invalid\\share\\x.png","//example.invalid/share/x.png","/\\example.invalid\\share\\x.png","\\/example.invalid/share/x.png",
        "\\\\?\\UNC\\example.invalid\\share\\x.png","\\??\\UNC\\example.invalid\\share\\x.png","\\\\.\\pipe\\x","file://example.invalid/x.png"})check(networkPath(unc),"network path not recognised");
    for(auto local:{"body.png","tex/body.png","..\\textures\\body.png","C:\\models\\body.png","C:/models/body.png","\\models\\body.png","C:body.png"})check(!networkPath(local),"local path taken for a network path");
    check(resolver.resolve("/\\example.invalid\\share\\body.png").path==dir/L"model"/L"body.png"&&resolver.resolve("\\??\\UNC\\example.invalid\\share\\body.png").repaired,"network texture reference did not fall back to the local file name");
    writeAtomic(dir/L"textures"/L"normal_map.png",data);
    TextureResolver ambiguous(dir/L"model");
    rejects([&]{ambiguous.resolve("normal map.png");},"ambiguous texture alias was selected");
    fs::create_directories(dir/L"model"/L"nested");
    writeAtomic(dir/L"textures"/L"speaker.jpg.001.jpg",data);
    TextureResolver nested(dir/L"model"/L"nested");
    check(nested.resolve("speaker.jpeg").path==dir/L"textures"/L"speaker.jpg.001.jpg","Packed JPEG alias or grandparent package texture lookup failed");
    rejects([&]{nested.resolve("speaker_other.jpeg");},"Texture alias selected an unrelated image");
    fs::remove_all(dir);
}
