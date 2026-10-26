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
    // Images past the texture limit are scaled down to it, keeping the ratio, with one note.
    const Bytes widePNG={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,32,0,0,0,8,8,6,0,0,0,152,133,253,175,0,0,0,26,73,68,65,84,120,218,99,56,81,161,241,127,32,49,195,168,3,70,29,48,234,128,129,118,0,0,190,33,103,31,226,58,197,111,0,0,0,0,73,69,78,68,174,66,96,130};
    auto wide=makeTexture(widePNG,16);check(wide.width==16&&wide.height==4&&wide.sourceWidth==32&&wide.sourceHeight==8,"a texture past the limit was not scaled down with its ratio");
    auto square=makeTexture(atlasPNG,8);check(square.width==8&&square.height==8&&square.sourceWidth==16,"a square texture past the limit was not scaled down");
    auto kept=makeTexture(atlasPNG,16);check(kept.width==16&&kept.height==16&&!kept.sourceWidth,"a texture within the limit was scaled");
    auto noted=fixture();noted.manifest["warnings"]=Json::array();addTexture(noted,wide,"wide.png");addTexture(noted,wide,"wide.png");
    check(noted.manifest["warnings"].size()==1&&noted.manifest["warnings"][0].get<std::string>().starts_with("Large texture wide.png (32 x 8) was scaled down to 16 x 4"),"scaled texture note missing or repeated");
    // full_textures keeps textures up to 16384 (fullTextureLimits): a 4100 x 8 image is kept and
    // noted, its alpha analysed, and the bundle decodes with the budgets every game module uses,
    // not with 2.3.0's.
    check(parseOptions(Json::object()).limits.textureDimension==4096&&parseOptions({{"full_textures",false}}).limits.textureDimension==4096,"textures past 4096 kept without the option");
    auto full=parseOptions({{"full_textures",true}}).limits;
    check(full.textureDimension==16384&&full.packageBytes==fullTextureLimits().packageBytes&&full.expandedBytes==fullTextureLimits().expandedBytes,"full_textures does not raise the texture limit and budgets");
    rejects([]{parseOptions({{"full_textures","yes"}});},"a texture size option that is not a boolean accepted");
    Bytes tga(18,0);tga[2]=2;tga[12]=uint8_t(4100&255);tga[13]=uint8_t(4100>>8);tga[14]=8;tga[16]=32;tga[17]=0x28;
    for(int i=0;i<4100*8;i++){tga.push_back(uint8_t(i*7));tga.push_back(uint8_t(i*13));tga.push_back(uint8_t(i*29));tga.push_back(i%5?255:0);}
    auto shrunk=makeTexture(tga,4096);check(shrunk.width==4096&&shrunk.height==8&&shrunk.sourceWidth==4100,"a texture past 4096 was not scaled down by default");
    auto large=makeTexture(tga,full.textureDimension);check(large.width==4100&&large.height==8&&!large.sourceWidth,"full_textures scaled a texture within 16384");
    auto big=fixture();big.manifest["format"]="pmx";big.manifest["warnings"]=Json::array();addTexture(big,large,"wide.tga");big.manifest["materials"][0]["base_texture"]=large.hash;
    for(auto& v:big.vertices){v.u=v.pos.x<0?0.f:1.f;v.v=v.pos.y<0?.05f:.95f;}
    check(big.manifest["warnings"].size()==1&&big.manifest["warnings"][0]=="Large texture wide.tga (4100 x 8) was kept at full size; memory use and loading time are higher.","a texture kept at full size is not noted");
    analyzeMaterials(big);check(big.manifest["material_processing_version"]==2,"the alpha of a texture kept at full size was not analysed");
    auto bundle=encode(big);rejects([&]{decode(bundle);},"2.3.0's limits accepted a texture past 4096");
    auto loaded=decode(bundle,fullTextureLimits());check(loaded.textures.size()==1&&loaded.textures[0].width==4100&&loaded.id==sha256(bundle),"a bundle with a texture kept at full size does not decode with fullTextureLimits()");
}
