#pragma once
#include "material_processing.hpp"
// Original 16x16 RGBA fixtures: opaque atlas island, antialiased cutout, and glass.
void materialProcessingTests(){
    const Bytes atlasPNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,28,73,68,65,84,120,156,99,184,179,37,234,63,1,204,128,15,51,140,26,48,106,192,168,1,195,197,0,0,73,209,105,159,112,75,174,218,0,0,0,0,73,69,78,68,174,66,96,130};
    const Bytes cutoutPNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,31,73,68,65,84,120,156,99,184,179,37,234,63,30,220,0,197,12,184,48,195,168,1,163,6,140,26,48,92,12,0,0,74,89,105,175,15,137,125,88,0,0,0,0,73,69,78,68,174,66,96,130};
    const Bytes glassPNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,25,73,68,65,84,120,156,99,184,179,37,170,129,18,204,48,106,192,168,1,163,6,12,23,3,0,84,168,106,31,194,26,82,185,0,0,0,0,73,69,78,68,174,66,96,130};
    const Bytes partialPNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,32,73,68,65,84,120,156,99,184,179,37,234,63,37,152,97,212,128,33,96,64,3,18,30,162,6,12,124,32,142,4,3,0,43,195,228,168,83,36,184,124,0,0,0,0,73,69,78,68,174,66,96,130};
    const Bytes opaquePNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,16,0,0,0,16,8,6,0,0,0,31,243,255,97,0,0,0,26,73,68,65,84,120,156,99,184,179,37,234,63,37,152,97,212,128,81,3,70,13,24,46,6,0,0,157,55,233,31,202,232,213,231,0,0,0,0,73,69,78,68,174,66,96,130};
    auto model=[&](const Bytes& png,float u0=0,float u1=1){
        auto a=fixture();a.manifest["format"]="pmx";
        auto texture=makeTexture(png,4096);a.manifest["materials"][0]["base_texture"]=texture.hash;a.textures.push_back(texture);
        for(auto& v:a.vertices){v.u=v.pos.x<0?u0:u1;v.v=v.pos.y<0?.05f:.95f;}
        return a;
    };
    auto mode=[](const Asset& a){return a.manifest["materials"][0]["alpha_mode"].get<std::string>();};
    auto a=model(atlasPNG,.05f,.35f);a.manifest["materials"][0]["alpha_mode"]="blend";analyzeMaterials(a);
    check(mode(a)=="opaque","unused transparent atlas area made material translucent");
    check(a.manifest["material_processing_version"]==2,"material processing version absent");
    a=model(atlasPNG);analyzeMaterials(a);check(mode(a)=="mask","binary alpha should be a depth-writing cutout");
    a=model(cutoutPNG);analyzeMaterials(a);check(mode(a)=="mask","antialiased cutout edge misclassified as glass");
    a=model(glassPNG);analyzeMaterials(a);check(mode(a)=="blend","uniform alpha glass should retain transparency");
    a=model(partialPNG);analyzeMaterials(a);check(mode(a)=="blend","localized translucent region should retain transparency");
    a=model(opaquePNG);a.manifest["materials"][0]["color"][3]=.4;analyzeMaterials(a);check(mode(a)=="blend","material scalar opacity was lost");
    a=model(glassPNG);a.manifest["format"]="gltf";analyzeMaterials(a);check(mode(a)=="opaque","glTF OPAQUE mode must override texture alpha");
    a=model(glassPNG);a.manifest["materials"][0]["alpha_mode"]="mask";a.manifest["materials"][0]["alpha_explicit"]=true;analyzeMaterials(a);
    check(mode(a)=="mask","explicit alpha mask changed");
    a=model(atlasPNG,1.05f,1.35f);analyzeMaterials(a);check(mode(a)=="opaque","repeating UV material alpha island was lost");
    a=model(atlasPNG,-.95f,-.65f);analyzeMaterials(a);check(mode(a)=="opaque","negative repeating UV material alpha island was lost");
    a=model(atlasPNG,0,100);analyzeMaterials(a);check(a.manifest["materials"][0]["alpha_detection"]["method"]=="bounded_texture_fallback","UV coverage work limit not enforced");
    a=model(atlasPNG);a.vertices[2].u=NAN;rejects([&]{analyzeMaterials(a);},"NaN UV accepted by material coverage analysis");
    a=model(opaquePNG);analyzeMaterials(a);auto before=a.manifest;analyzeMaterials(a);check(a.manifest==before,"material reanalysis is not idempotent");
    check(a.textures[0].alphaKnown&&!a.textures[0].hasAlpha,"fresh opaque texture hint missing");
    auto cached=decode(encode(a));check(!cached.textures[0].alphaKnown,"cache supplied a trusted alpha hint");
}
