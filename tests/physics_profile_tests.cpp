// Carrier physics profiles: the PHY text, MDL bytes and rig keys of unedited
// carriers are pinned by tests/fixtures/physics/golden.json, recorded with
// --record on the tree before the physics editor existed.
#include "rig.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace mmd;
namespace {
// Verbatim copy of the text section bridge.cpp wrote before physicsText existed.
std::string legacyPhysicsText(const Rig& rig){
    std::ostringstream kv;float bias=0;for(auto& b:rig.bodies)bias+=b.massBias;
    for(size_t i=0;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"solid {\n\"index\" \""<<i<<"\"\n\"name\" \""<<rig.bones[b.bone].name<<"\"\n";if(b.parent>=0)kv<<"\"parent\" \""<<rig.bones[rig.bodies[b.parent].bone].name<<"\"\n";
        kv<<"\"mass\" \""<<rig.mass*b.massBias/bias<<"\"\n\"surfaceprop\" \"flesh\"\n\"damping\" \"0.8\"\n\"rotdamping\" \""<<b.rotationDamping<<"\"\n\"inertia\" \"12\"\n}\n";
    }
    for(size_t i=1;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"ragdollconstraint {\n\"parent\" \""<<b.parent<<"\"\n\"child\" \""<<i<<"\"\n";for(int k=0;k<3;k++){char axis='x'+char(k);kv<<'"'<<axis<<"min\" \""<<b.lower[k]<<"\"\n\""<<axis<<"max\" \""<<b.upper[k]<<"\"\n\""<<axis<<"friction\" \"0\"\n";}kv<<"}\n";}
    kv<<"collisionrules {\n";for(size_t a=0;a<18;a++)for(size_t b=a+1;b<18;b++)if(rig.bodies[b].parent!=int(a)&&rig.bodies[a].parent!=int(b))kv<<"\"collisionpair\" \""<<a<<","<<b<<"\"\n";kv<<"}\neditparams {\n\"rootname\" \"ValveBiped.Bip01_Pelvis\"\n\"totalmass\" \""<<rig.mass<<"\"\n}\n";
    return kv.str();
}
std::string mdlHash(const Rig& r){auto files=carrierFiles(r);auto& mdl=files.at(r.path);return hash(std::span(mdl.data(),mdl.size()));}
const char* Fixture="tests/fixtures/native-cloth21.pmx";
const char* Golden="tests/fixtures/physics/golden.json";
// Each case fits the fixture through the full path ("full") or the cached fast path ("cached").
Rig fitCase(const Json& c){
    auto m=parse(readFile(Fixture));
    if(c.at("path")=="cached")m->fittedRig=std::make_shared<Rig>(fitRig(*m,Json::object()));
    return fitRig(*m,c.at("options"));
}
Json goldenCases(){
    Json cases=Json::array();
    for(float s:{.5f,1.f,1.7f})for(auto path:{"full","cached"})cases.push_back({{"path",path},{"options",{{"scaleMultiplier",s}}}});
    cases.push_back({{"path","full"},{"options",{{"mass",62},{"excludedMaterials",Json::array({999})}}}});
    // A collision correction on both paths: its hull, faces and key stay pinned too.
    auto m=parse(readFile(Fixture));auto base=fitRig(*m,Json::object());Json overrides=Json::object();
    for(auto& body:base.manifest["bodies"])if(body["name"]=="ValveBiped.Bip01_L_Forearm"||body["name"]=="ValveBiped.Bip01_Head1"){
        Json center=Json::array(),extent=Json::array();for(int k=0;k<3;k++){center.push_back(std::round(body["center"][k].get<double>()*1.05e4)/1e4);extent.push_back(std::round(body["extent"][k].get<double>()*1.2e4)/1e4);}
        overrides[body["name"].get<std::string>()]={{"center",center},{"extent",extent}};
    }
    for(auto path:{"full","cached"})cases.push_back({{"path",path},{"options",{{"collisionOverrides",overrides},{"collisionOverrideScale",base.scale}}}});
    return cases;
}
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--record"){
            Json out={{"fixture",Fixture},{"cases",Json::array()}};
            for(auto c:goldenCases()){auto rig=fitCase(c);c["key"]=rig.key;c["mdlSha256"]=mdlHash(rig);c["phy"]=legacyPhysicsText(rig);out["cases"].push_back(c);}
            fs::create_directories(fs::path(Golden).parent_path());std::ofstream(Golden,std::ios::binary)<<out.dump(1)<<"\n";
            std::cout<<"Recorded "<<out["cases"].size()<<" golden carriers\n";return 0;
        }
        int failed=0,passed=0;auto check=[&](bool ok,const std::string& label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<"\n";(ok?passed:failed)++;};
        auto golden=readJson(Golden);
        for(auto& c:golden.at("cases")){
            auto rig=fitCase(c);auto label=c.at("path").get<std::string>()+" "+c.at("options").dump().substr(0,60);
            check(rig.key==c.at("key"),"golden rig key: "+label);
            check(mdlHash(rig)==c.at("mdlSha256"),"golden MDL bytes: "+label);
            check(legacyPhysicsText(rig)==c.at("phy"),"golden legacy PHY text: "+label);
        }
        std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
