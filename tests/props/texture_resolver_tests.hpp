#include "texture_resolver.hpp"
#include <cstring>
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
    // A model names only its own files: never one outside its folder and the tex and
    // textures folders near it, however the reference is written. Its bytes would go into
    // the cache and to other players with the model.
    fs::create_directories(dir/L"outside");writeAtomic(dir/L"outside"/L"secret.png",data);writeAtomic(dir/L"secret.png",data);
    TextureResolver confined(dir/L"model");
    for(auto escape:{std::string("../secret.png"),std::string("..\\outside\\secret.png"),std::string("tex/../../outside/secret.png"),std::string("C:secret.png"),std::string("\\secret.png"),
        utf8((dir/L"outside"/L"secret.png").wstring()),utf8((dir/L"secret.png").wstring()),utf8((dir/L"outside"/L"secret.png").generic_wstring())})
        rejects([&]{confined.resolve(escape);},"a texture outside the model's folders was read");
    // ...nor a download's Zone.Identifier stream (its source address), nor one a junction leads to.
    {std::ofstream stream(dir/L"model"/L"body.png:Zone.Identifier");stream<<"[ZoneTransfer]\nHostUrl=https://example.invalid/private-link\n";}
    rejects([&]{confined.resolve("body.png:Zone.Identifier");},"an alternate data stream was read as a texture");
    auto junction=[](const fs::path& link,const fs::path& target){auto cmd=L"cmd /c mklink /J \""+link.wstring()+L"\" \""+target.wstring()+L"\" >nul";return _wsystem(cmd.c_str())==0;};
    check(junction(dir/L"model"/L"linked",dir/L"outside"),"cannot create the test junction");
    rejects([&]{confined.resolve("linked/secret.png");},"a junction in the model's folder led a texture outside it");
    // Textures in the tex and textures folders beside and above the model (and below them), and
    // absolute paths into the model's own folder, still resolve as written.
    fs::create_directories(dir/L"textures"/L"skin");writeAtomic(dir/L"textures"/L"skin"/L"face.png",data);
    auto sibling=confined.resolve("../textures/skin/face.png");
    check(sibling.path==dir/L"textures"/L"skin"/L"face.png"&&!sibling.repaired,"a texture below the textures folder beside the model no longer resolves as written");
    auto inside=confined.resolve(utf8((dir/L"model"/L"tex"/L"normal_map.png").wstring()));
    check(inside.path==dir/L"model"/L"tex"/L"normal_map.png"&&!inside.repaired,"an absolute reference into the model's own folder no longer resolves");
    check(confined.resolve("tex/../body.png").path==dir/L"model"/L"body.png","a reference that stays in the model's folder no longer resolves");
    // The worker's denylist (file access's never-readable places) applies to every texture.
    writeAtomic(dir/L"model"/L"wallet.png",data);
    setDependencyDenylist([](const fs::path& p){return p.filename()==L"wallet.png";});
    rejects([&]{TextureResolver(dir/L"model").resolve("wallet.png");},"a denied file was read as a texture");
    setDependencyDenylist({});
    check(TextureResolver(dir/L"model").resolve("wallet.png").path==dir/L"model"/L"wallet.png","the denylist outlived its process setting");
    // The model's buffers (glTF) come only from its own folder too: a reference that climbs
    // out fails the import instead of reading another file. (Assimp then tries the file name
    // in the model's folder, so the outside file has a name of its own.)
    {std::vector<float> points{0,0,0, 1,0,0, 0,1,0, 0,0,1};std::vector<uint16_t> faces{0,2,1, 0,1,3, 0,3,2, 1,2,3};
     Bytes bin(points.size()*4+faces.size()*2);std::memcpy(bin.data(),points.data(),points.size()*4);std::memcpy(bin.data()+points.size()*4,faces.data(),faces.size()*2);
     auto gltf=[&](const std::string& uri){return Json{{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",{{{"nodes",{0}}}}},{"nodes",{{{"mesh",0}}}},
         {"meshes",{{{"primitives",{{{"attributes",{{"POSITION",0}}},{"indices",1}}}}}}},
         {"buffers",{{{"uri",uri},{"byteLength",bin.size()}}}},
         {"bufferViews",{{{"buffer",0},{"byteOffset",0},{"byteLength",points.size()*4}},{{"buffer",0},{"byteOffset",points.size()*4},{"byteLength",faces.size()*2}}}},
         {"accessors",{{{"bufferView",0},{"componentType",5126},{"count",4},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,1,1}}},{{"bufferView",1},{"componentType",5123},{"count",12},{"type","SCALAR"}}}}}.dump();};
     auto write=[](const fs::path& p,const std::string& text){writeAtomic(p,Bytes(text.begin(),text.end()));};
     writeAtomic(dir/L"model"/L"shape.bin",bin);writeAtomic(dir/L"outside"/L"far.bin",bin);
     write(dir/L"model"/L"beside.gltf",gltf("shape.bin"));write(dir/L"model"/L"escape.gltf",gltf("../outside/far.bin"));write(dir/L"model"/L"linked.gltf",gltf("linked/far.bin"));
     Options options;
     auto beside=importModel(dir/L"model"/L"beside.gltf",options,{});
     check(beside.vertices.size()==4&&beside.indices.size()==12,"a glTF buffer beside the model no longer imports");
     rejects([&]{importModel(dir/L"model"/L"escape.gltf",options,{});},"a glTF buffer outside the model's folder was read");
     rejects([&]{importModel(dir/L"model"/L"linked.gltf",options,{});},"a glTF buffer behind a junction was read");
     // A .gmodel.json sidecar can come with a downloaded model: its textures are read from the
     // model's folders too. One outside them is left out with a warning naming only the file,
     // and the rest of the override still applies; one inside them still repairs the material;
     // one that is nowhere still fails the import.
     const Bytes png={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,26,73,68,65,84,120,156,99,184,179,37,234,63,37,152,97,212,128,81,3,70,13,24,46,6,0,0,157,55,233,31,202,232,213,231,0,0,0,0,73,69,78,68,174,66,96,130};
     writeAtomic(dir/L"outside"/L"fix.png",png);writeAtomic(dir/L"model"/L"paint.png",png);
     auto painted=[&](const std::string& name,const Json& skin){auto j=Json::parse(gltf("shape.bin"));j["materials"]={{{"name","skin"}}};j["meshes"][0]["primitives"][0]["material"]=0;
         write(dir/L"model"/wide(name),j.dump());write(dir/L"model"/wide(name+".gmodel.json"),Json{{"version",1},{"materials",{{"skin",skin}}}}.dump());return dir/L"model"/wide(name);};
     auto warned=[](const Asset& a,const std::string& text){for(auto& w:a.manifest["warnings"])if(w.get<std::string>()==text)return true;return false;};
     auto skin=[](const Asset& a){for(auto& m:a.manifest["materials"])if(m.value("name","")=="skin")return m;return Json();};
     const std::string refused="Material override texture outside the model's folders was not used: fix.png";
     for(auto ref:{utf8((dir/L"outside"/L"fix.png").wstring()),std::string("../outside/fix.png")}){
         auto a=importModel(painted("outside.gltf",{{"base_texture",ref},{"color",{1,0,0,1}}}),options,{});
         check(warned(a,refused)&&skin(a).value("base_texture","").empty()&&skin(a)["color"][1]==0,"a sidecar texture outside the model's folders failed the import, was read or dropped the rest of the override");
         for(auto& w:a.manifest["warnings"])check(w.get<std::string>().find(utf8(dir.wstring()))==std::string::npos,"a sidecar warning names a folder on this computer");}
     auto inside=importModel(painted("inside.gltf",{{"base_texture","paint.png"}}),options,{});
     check(!skin(inside).value("base_texture","").empty()&&!warned(inside,refused),"a sidecar texture beside the model no longer repairs the material");
     auto masked=importModel(painted("masked.gltf",{{"base_texture","paint.png"},{"opacity_texture","../outside/fix.png"}}),options,{});
     check(!skin(masked).value("base_texture","").empty()&&warned(masked,refused),"a sidecar's opacity texture outside the model's folders was read, or lost the base texture");
     rejects([&]{importModel(painted("nowhere.gltf",{{"base_texture","nowhere.png"}}),options,{});},"a sidecar texture that is nowhere no longer fails the import");
     // Where Windows cannot say where an open file really is (some RAM disks, FUSE and cloud
     // drives), files with no link on the way still resolve; a junction in the model's folder
     // and a tex folder that is a junction still lead nowhere, and the denylist still applies.
     mmd::simulateUnknownFinalPaths(true);
     {TextureResolver blind(dir/L"model");auto body=blind.resolve("body.png");auto face=blind.resolve("../textures/skin/face.png");
      check(body.path==dir/L"model"/L"body.png"&&!body.repaired&&face.path==dir/L"textures"/L"skin"/L"face.png"&&!face.repaired,"textures no longer resolve where Windows cannot say where a file really is");
      rejects([&]{blind.resolve("linked/secret.png");},"a junction led a texture out where Windows cannot say where a file really is");
      check(importModel(dir/L"model"/L"beside.gltf",options,{}).vertices.size()==4,"a glTF buffer no longer imports where Windows cannot say where a file really is");
      rejects([&]{importModel(dir/L"model"/L"linked.gltf",options,{});},"a glTF buffer behind a junction was read where Windows cannot say where a file really is");
      check(junction(dir/L"tex",dir/L"outside"),"cannot create the tex junction");
      rejects([&]{TextureResolver(dir/L"model").resolve("../tex/secret.png");},"a tex folder that is a junction led a texture out where Windows cannot say where a file really is");
      fs::remove(dir/L"tex");
      setDependencyDenylist([](const fs::path& p){return p.filename()==L"wallet.png";});
      rejects([&]{TextureResolver(dir/L"model").resolve("wallet.png");},"a denied file was read where Windows cannot say where a file really is");
      setDependencyDenylist({});}
     mmd::simulateUnknownFinalPaths(false);}
    fs::remove(dir/L"model"/L"linked");
    fs::remove_all(dir);
}
