// Carrier physics profiles (physics_profile.cpp): canonical physicsOverrides,
// the .phy text, masses, shape styles and the editor's preview. The PHY text,
// MDL bytes and rig keys of unedited carriers are pinned by
// tests/fixtures/physics/golden.json, recorded with --record on the tree
// before the physics editor existed. Re-record it only for a deliberate change
// to the carrier fit itself, and only while every "legacy" check here passes.
#include "physics_profile.hpp"
#include "rig_animation.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <set>
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
// Copy of module.cpp's fitKey before physicsOverrides joined it.
std::string legacyFitKey(const std::string& id,const Json& options){Json geometry;for(auto key:{"scaleMultiplier","scale","height","mass","collisionOverrides","collisionOverrideScale","excludedMaterials","role","gender","animationSource","animationReference","armsParts"})if(options.contains(key))geometry[key]=options[key];return id+geometry.dump();}
std::string mdlHash(const Rig& r){auto files=carrierFiles(r);auto& mdl=files.at(r.path);return hash(std::span(mdl.data(),mdl.size()));}
const char* Fixture="tests/fixtures/native-cloth21.pmx";
const char* Golden="tests/fixtures/physics/golden.json";
std::shared_ptr<Model> fixture(bool cached){auto m=parse(readFile(Fixture));if(cached)m->fittedRig=std::make_shared<Rig>(fitRig(*m,Json::object()));return m;}
// Each case fits the fixture through the full path ("full") or the cached fast path ("cached").
Rig fitCase(const Json& c){auto m=fixture(c.at("path")=="cached");return fitRig(*m,c.at("options"));}
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
std::string block(const std::string& text,const std::string& start){auto at=text.find(start);if(at==text.npos)return {};return text.substr(at,text.find("}\n",at)-at+2);}
std::string solid(const std::string& text,int i){return block(text,"solid {\n\"index\" \""+std::to_string(i)+"\"\n");}
std::string constraint(const std::string& text,int i){auto at=text.find("\"child\" \""+std::to_string(i)+"\"\n");if(at==text.npos)return {};auto start=text.rfind("ragdollconstraint {",at);return text.substr(start,text.find("}\n",at)-start+2);}
std::vector<std::string> lines(const std::string& text){std::vector<std::string> out;std::istringstream in(text);for(std::string line;std::getline(in,line);)out.push_back(line);return out;}
size_t count(const std::string& text,const std::string& what){size_t n=0;for(auto at=text.find(what);at!=text.npos;at=text.find(what,at+1))n++;return n;}
std::string printed(float v){std::ostringstream s;s<<v;return s.str();}
int32_t i32(const Bytes& b,size_t p){int32_t v;std::memcpy(&v,b.data()+p,4);return v;}
std::string cstring(const Bytes& b,size_t p){std::string s;while(p<b.size()&&b[p])s+=char(b[p++]);return s;}
// Surface property strings of an MDL: the header's, then each bone's.
std::pair<std::string,std::vector<std::string>> mdlSurfaces(const Rig& r){
    auto files=carrierFiles(r);auto& mdl=files.at(r.path);std::vector<std::string> bones;size_t table=size_t(i32(mdl,160));
    for(int k=0;k<i32(mdl,156);k++){size_t p=table+size_t(k)*216;bones.push_back(cstring(mdl,p+size_t(i32(mdl,p+176))));}
    return {cstring(mdl,size_t(i32(mdl,308))),bones};
}
int bodyIndex(const char* shortName){for(int i=0;i<18;i++)if(std::string(CarrierBodyNames[i])=="ValveBiped.Bip01_"+std::string(shortName))return i;return -1;}
Json profile(const Json& bodies,const Json& model=Json::object()){Json p={{"schema",1}};p.update(model);if(!bodies.empty())p["bodies"]=bodies;return p;}
Json body(const char* shortName,const Json& fields){return {{std::string("ValveBiped.Bip01_")+shortName,fields}};}
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--record"){
            Json out={{"fixture",Fixture},{"cases",Json::array()}};
            for(auto c:goldenCases()){auto rig=fitCase(c);c["key"]=rig.key;c["mdlSha256"]=mdlHash(rig);c["phy"]=legacyPhysicsText(rig);out["cases"].push_back(c);}
            fs::create_directories(fs::path(Golden).parent_path());std::ofstream(Golden,std::ios::binary)<<out.dump(1)<<"\n";
            std::cout<<"Recorded "<<out["cases"].size()<<" golden carriers\n";return 0;
        }
        // Offline check on other models: unedited carriers write the 2.2 text (never run by CTest).
        if(argc>2&&std::string(argv[1])=="--compare"){
            int differ=0;for(int i=2;i<argc;i++){try{auto m=parse(readFile(fs::path(wide(argv[i]))));for(bool path:{false,true}){if(path)m->fittedRig=std::make_shared<Rig>(fitRig(*m,Json::object()));auto rig=fitRig(*m,Json::object());bool same=physicsText(rig)==legacyPhysicsText(rig)&&!rig.manifest.contains("physicsOverrides");differ+=!same;std::cout<<(same?"same ":"DIFFERENT ")<<(path?"cached ":"full ")<<rig.key<<" "<<argv[i]<<"\n";}}catch(const std::exception& e){std::cout<<"skipped "<<argv[i]<<": "<<e.what()<<"\n";}}
            return differ?1:0;
        }
        int failed=0,passed=0;auto check=[&](bool ok,const std::string& label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<"\n";(ok?passed:failed)++;};
        auto rejects=[](const std::function<void()>& f){try{f();}catch(const std::exception&){return true;}return false;};
        // N1/N2: unedited carriers keep their golden key, MDL bytes and .phy text.
        auto golden=readJson(Golden);
        for(auto& c:golden.at("cases")){
            auto rig=fitCase(c);auto label=c.at("path").get<std::string>()+" "+c.at("options").dump().substr(0,60);
            check(rig.key==c.at("key"),"golden rig key: "+label);
            check(mdlHash(rig)==c.at("mdlSha256"),"golden MDL bytes: "+label);
            check(legacyPhysicsText(rig)==c.at("phy"),"golden legacy PHY text: "+label);
            check(physicsText(rig)==c.at("phy"),"physicsText reproduces the golden text byte for byte: "+label);
            check(!rig.manifest.contains("physicsOverrides")&&!rig.manifest.contains("physicsWriter"),"unedited manifest has no physics keys: "+label);
        }
        auto full=fixture(false),cached=fixture(true);auto base=fitRig(*cached,Json::object());const auto basePhy=physicsText(base);const auto baseMdl=mdlHash(base);
        // N3: every spelling of "no change" shares one carrier.
        {
            Json spelled=profile(Json::object(),{{"surfaceprop","flesh"},{"massMode","bias"},{"collisions",{{"mode","all"}}}});
            for(int i=0;i<18;i++){auto& d=carrierBodyDefaults()[i];Json fields={{"massBias",d.massBias},{"damping",.8},{"rotdamping",d.rotationDamping},{"inertia",12},{"surfaceprop","flesh"}};
                if(i>0)for(int k=0;k<3;k++)fields["limits"][std::string(1,char('x'+k))]={d.lower[k],d.upper[k],0};spelled["bodies"][CarrierBodyNames[i]]=fields;}
            Json pairs=Json::array();for(auto [a,b]:enabledPairs(Json::object()))pairs.push_back({b,a});
            Json allCustom=profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",pairs}}}});
            for(bool path:{false,true}){
                auto& m=path?*cached:*full;auto reference=fitRig(m,Json::object());
                std::vector<Json> same={{{"mass",70}},{{"physicsOverrides",Json::object()}},{{"physicsOverrides",Json::array()}},{{"physicsOverrides",nullptr}},{{"physicsOverrides",{{"schema",1}}}},{{"physicsOverrides",spelled}},{{"physicsOverrides",allCustom}}};
                bool keys=true,clean=true,bytes=true;
                for(auto& options:same){auto rig=fitRig(m,options);keys&=rig.key==reference.key;clean&=!rig.manifest.contains("physicsOverrides")&&!rig.manifest.contains("physicsWriter");bytes&=mdlHash(rig)==mdlHash(reference)&&physicsText(rig)==physicsText(reference);}
                check(keys&&clean&&bytes,std::string("default spellings share the unedited key, manifest and bytes (")+(path?"cached":"full")+" fit)");
            }
            auto [header,bones]=mdlSurfaces(base);bool flesh=header=="flesh";for(auto& s:bones)flesh&=s=="flesh";
            check(flesh,"an unedited MDL names flesh in its header and on every bone");
        }
        // N4: each field changes the key and writes its exact line.
        {
            struct Case {std::string label;Json physics;std::function<bool(const Rig&,const std::string&)> expect;};
            const int forearm=bodyIndex("L_Forearm"),hand=bodyIndex("L_Hand"),head=bodyIndex("Head1"),spine=bodyIndex("Spine1");
            std::vector<Case> cases={
             {"limit x",profile(body("L_Forearm",{{"limits",{{"x",{-30,11.3,0}}}}})),[&](const Rig&,const std::string& t){auto c=constraint(t,forearm);return c.find("\"xmin\" \"-30\"\n\"xmax\" \"11.3\"\n\"xfriction\" \"0\"")!=c.npos;}},
             {"limit y",profile(body("L_Forearm",{{"limits",{{"y",{-5,5,0.2}}}}})),[&](const Rig&,const std::string& t){auto c=constraint(t,forearm);return c.find("\"ymin\" \"-5\"\n\"ymax\" \"5\"\n\"yfriction\" \"0.2\"")!=c.npos;}},
             {"limit z",profile(body("L_Forearm",{{"limits",{{"z",{-90,7.5,0.6}}}}})),[&](const Rig&,const std::string& t){auto c=constraint(t,forearm);return c.find("\"zmin\" \"-90\"\n\"zmax\" \"7.5\"\n\"zfriction\" \"0.6\"")!=c.npos;}},
             {"free axis",profile(body("L_Hand",{{"limits",{{"x",{-360,360,1}}}}})),[&](const Rig&,const std::string& t){auto c=constraint(t,hand);return c.find("\"xmin\" \"-360\"\n\"xmax\" \"360\"\n\"xfriction\" \"1\"")!=c.npos;}},
             {"massBias",profile(body("L_Hand",{{"massBias",2}})),[&](const Rig&,const std::string& t){return solid(t,hand).find("\"mass\" \""+printed(70.f*2.f/75.f)+"\"")!=std::string::npos;}},
             {"damping",profile(body("Head1",{{"damping",1.5}})),[&](const Rig&,const std::string& t){return solid(t,head).find("\"damping\" \"1.5\"")!=std::string::npos;}},
             {"rotdamping",profile(body("Spine1",{{"rotdamping",10}})),[&](const Rig&,const std::string& t){return solid(t,spine).find("\"rotdamping\" \"10\"")!=std::string::npos;}},
             {"inertia",profile(body("Pelvis",{{"inertia",6}})),[&](const Rig&,const std::string& t){return solid(t,0).find("\"inertia\" \"6\"")!=std::string::npos;}},
             {"drag before inertia",profile(body("Head1",{{"drag",1}})),[&](const Rig&,const std::string& t){return solid(t,head).find("\"rotdamping\" \"3\"\n\"drag\" \"1\"\n\"inertia\" \"12\"")!=std::string::npos&&count(t,"\"drag\"")==1;}},
             {"body surfaceprop",profile(body("Head1",{{"surfaceprop","metal"}})),[&](const Rig& r,const std::string& t){
                if(solid(t,head).find("\"surfaceprop\" \"metal\"")==std::string::npos||count(t,"\"surfaceprop\" \"metal\"")!=1)return false;
                auto [header,bones]=mdlSurfaces(r);bool ok=header=="flesh";
                for(size_t k=0;k<r.bones.size();k++){int a=int(k);while(a>=0&&r.bones[a].physics<0)a=r.bones[a].parent;ok&=bones[k]==(a>=0&&r.bones[a].physics==head?"metal":"flesh");}
                int inherited=0;for(size_t k=0;k<r.bones.size();k++)inherited+=r.bones[k].physics<0&&bones[k]=="metal";return ok&&inherited>0;}},
             {"model surfaceprop",profile(body("L_Hand",{{"surfaceprop","rubber"}}),{{"surfaceprop","wood"}}),[&](const Rig& r,const std::string& t){
                auto [header,bones]=mdlSurfaces(r);return header=="wood"&&count(t,"\"surfaceprop\" \"wood\"")==17&&solid(t,hand).find("\"surfaceprop\" \"rubber\"")!=std::string::npos&&bones[r.bodies[hand].bone]=="rubber"&&bones[r.bodies[0].bone]=="wood";}},
             {"volume weighting",profile(Json::object(),{{"massMode","volume"}}),[&](const Rig& r,const std::string& t){
                double weighted=0;for(size_t i=0;i<18;i++)weighted+=hullVolume(r.bodies[i].hull,r.manifest["bodies"][i]["faces"])*r.bodies[i].massBias;
                bool ok=true;for(size_t i=0;i<18;i++)ok&=solid(t,int(i)).find("\"mass\" \""+printed(std::max(1.f,float(70.*hullVolume(r.bodies[i].hull,r.manifest["bodies"][i]["faces"])*r.bodies[i].massBias/weighted)))+"\"")!=std::string::npos;
                return ok&&t.find("\"totalmass\" \"70\"")!=t.npos;}},
             {"automass",profile(Json::object(),{{"automass",{{"density",985}}}}),[&](const Rig& r,const std::string& t){return r.mass!=70&&t.find("\"totalmass\" \""+printed(r.mass)+"\"")!=t.npos&&r.manifest["mass"].get<float>()==r.mass;}},
             {"no self-collision",profile(Json::object(),{{"collisions",{{"mode","none"}}}}),[&](const Rig&,const std::string& t){return block(t,"collisionrules {")=="collisionrules {\n\"selfcollisions\" \"0\"\n}\n";}},
             {"custom pairs",profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",{{3,7},{3,11}}}}}}),[&](const Rig&,const std::string& t){return block(t,"collisionrules {")=="collisionrules {\n\"collisionpair\" \"3,7\"\n\"collisionpair\" \"3,11\"\n}\n";}},
             {"animated friction",profile(Json::object(),{{"animatedFriction",{{"min",80},{"max",600},{"timeIn",.15},{"timeOut",.25},{"timeHold",1.75}}}}),[&](const Rig&,const std::string& t){
                return block(t,"animatedfriction {")=="animatedfriction {\n\"animfrictionmin\" \"80\"\n\"animfrictionmax\" \"600\"\n\"animfrictiontimein\" \"0.15\"\n\"animfrictiontimeout\" \"0.25\"\n\"animfrictiontimehold\" \"1.75\"\n}\n"&&t.find("}\nanimatedfriction {")<t.find("editparams {");}},
            };
            std::set<std::string> keys{base.key};
            for(auto& c:cases){
                for(bool path:{true,false}){auto rig=fitRig(path?*cached:*full,{{"physicsOverrides",c.physics}});auto text=physicsText(rig);
                    check(rig.key!=(path?base.key:fitRig(*full,Json::object()).key)&&c.expect(rig,text)&&rig.manifest["physicsWriter"]==PhysicsWriterVersion,"profile field "+c.label+(path?" (cached fit)":" (full fit)"));
                    if(path)keys.insert(rig.key);}
            }
            check(keys.size()==cases.size()+1,"every profile field gives its own carrier key");
        }
        // N5: float noise and pair spelling never create a second carrier.
        {
            auto a=profile(body("L_Forearm",{{"limits",{{"z",{-90,10.0000001,0}}}},{"damping",0.80000000000000004},{"inertia",11.9999999}}),{{"collisions",{{"mode","custom"},{"pairs",{{7,3},{3,11},{3,7},{11,3}}}}}});
            auto b=profile(body("L_Forearm",{{"limits",{{"z",{-90,10,0}}}},{"inertia",12}}),{{"collisions",{{"mode","custom"},{"pairs",{{3,7},{3,11}}}}}});
            auto ca=canonicalPhysics(a).value,cb=canonicalPhysics(b).value;
            check(ca.dump()==cb.dump()&&!ca.empty()&&fitRig(*cached,{{"physicsOverrides",a}}).key==fitRig(*cached,{{"physicsOverrides",b}}).key,"quantising and pair normalisation remove float noise and ordering");
            check(ca["bodies"].size()==1&&!ca["bodies"].begin()->contains("damping")&&!ca["bodies"].begin()->contains("inertia"),"values that round to their default are dropped");
        }
        // N6: canonical forms.
        {
            auto c=canonicalPhysics(profile(body("L_Hand",{{"limits",{{"x",{0,0,0.5}},{"y",{-360,360,0.4}},{"z",{-0.0,20,0}}}}}))).value;auto& l=c["bodies"]["ValveBiped.Bip01_L_Hand"]["limits"];
            check(l["x"]==Json::array({0,0,0})&&l["y"]==Json::array({-360,360,0.4})&&!std::signbit(l["z"][0].get<double>()),"fixed axes drop friction, free axes keep it, -0 becomes 0");
            check(canonicalPhysics(profile(body("L_Forearm",{{"limits",{{"y",{0,0,0.5}}}}}))).value.empty(),"a fixed axis equal to the SCMI default is dropped");
            check(canonicalPhysics(profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",Json::array()}}}})).value==Json({{"schema",1},{"collisions",{{"mode","none"}}}}),"an empty custom pair set is mode none");
            Json all=Json::array();for(auto [a,b]:enabledPairs(Json::object()))all.push_back({a,b});
            check(canonicalPhysics(profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",all}}}})).value.empty(),"all 136 pairs is the default rule set");
            check(canonicalPhysics(profile(Json::array(),{{"bodies",Json::array()},{"automass",{{"density",1000.04}}}})).value==Json({{"schema",1},{"automass",{{"density",1000}}}}),"Lua's empty tables are accepted as objects; density keeps one decimal");
        }
        // N7: every rejection code, and the body table matches fitRig.
        {
            std::vector<std::pair<std::string,Json>> bad={
             {"not_finite",profile(body("L_Hand",{{"damping","fast"}}))},
             {"limit_order",profile(body("L_Hand",{{"limits",{{"x",{10,-10,0}}}}}))},
             {"limit_range",profile(body("L_Hand",{{"limits",{{"x",{-10,181,0}}}}}))},
             {"limit_range",profile(body("L_Hand",{{"limits",{{"x",{-10,10}}}}}))},
             {"friction_range",profile(body("L_Hand",{{"limits",{{"x",{-10,10,-1}}}}}))},
             {"unknown_body",profile({{"ValveBiped.Bip01_Spine2",{{"damping",1}}}})},
             {"root_joint",profile(body("Pelvis",{{"limits",{{"x",{-10,10,0}}}}}))},
             {"pair_index",profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",{{3,3}}}}}})},
             {"pair_index",profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",{{3,18}}}}}})},
             {"pair_adjacent",profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",{{0,1}}}}}})},
             {"collision_mode",profile(Json::object(),{{"collisions",{{"mode","some"}}}})},
             {"surfaceprop",profile(body("L_Hand",{{"surfaceprop","Flesh!"}}))},
             {"surfaceprop",profile(Json::object(),{{"surfaceprop",std::string(33,'a')}})},
             {"animated_friction_range",profile(Json::object(),{{"animatedFriction",{{"min",600},{"max",80},{"timeIn",.1},{"timeOut",.1},{"timeHold",.1}}}})},
             {"not_integer",profile(Json::object(),{{"animatedFriction",{{"min",80.5},{"max",600},{"timeIn",.1},{"timeOut",.1},{"timeHold",.1}}}})},
             {"unknown_field",profile(body("L_Hand",{{"massbias",2}}))},
             {"unknown_field",profile(Json::object(),{{"preset","statue"}})},
             {"schema_unsupported",{{"schema",2},{"massMode","volume"}}},
             {"schema_unsupported",{{"massMode","volume"}}},
             {"mass_bias_range",profile(body("L_Hand",{{"massBias",0}}))},
             {"damping_range",profile(body("L_Hand",{{"damping",11}}))},
             {"rotdamping_range",profile(body("L_Hand",{{"rotdamping",101}}))},
             {"inertia_range",profile(body("L_Hand",{{"inertia",.05}}))},
             {"drag_range",profile(body("L_Hand",{{"drag",-1}}))},
             {"mass_mode",profile(Json::object(),{{"massMode","heavy"}})},
             {"density_range",profile(Json::object(),{{"automass",{{"density",5}}}})},
             {"not_object",profile(Json::object(),{{"bodies",{{"ValveBiped.Bip01_L_Hand",3}}}})},
            };
            for(auto& [code,raw]:bad){auto c=canonicalPhysics(raw);bool thrown=false;std::string message;try{requireCanonicalPhysics(raw);}catch(const std::exception& e){thrown=true;message=e.what();}
                check(!c.errors.empty()&&c.errors.front().code==code&&c.value.empty()&&thrown&&message.starts_with("Invalid physics settings: "+code+" ")&&rejects([&]{fitRig(*cached,{{"physicsOverrides",raw}});}),"rejects "+code+": "+raw.dump().substr(0,70));}
            auto nan=profile(body("L_Hand",{{"damping",std::nan("")}}));check(canonicalPhysics(nan).errors.front().code=="not_finite","rejects NaN as not_finite");
            check(canonicalPhysics(profile(body("L_Hand",{{"massbias",2}}),{{"preset","x"}})).errors.size()==2,"every error is reported, not only the first");
            bool names=base.manifest["bodies"].size()==18;for(int i=0;i<18;i++)names&=base.manifest["bodies"][i]["name"]==CarrierBodyNames[i]&&base.bodies[i].parent==CarrierBodyParents[i];
            check(names,"CarrierBodyNames and CarrierBodyParents match fitRig's bodies");
            bool defaults=true;for(int i=0;i<18;i++){auto& d=carrierBodyDefaults()[i];defaults&=d.massBias==base.bodies[i].massBias&&d.rotationDamping==base.bodies[i].rotationDamping&&d.lower==base.bodies[i].lower&&d.upper==base.bodies[i].upper;}
            check(defaults,"carrierBodyDefaults equal the fitted SCMI values");
        }
        // N8: collision rule text.
        {
            auto none=physicsText(fitRig(*cached,{{"physicsOverrides",profile(Json::object(),{{"collisions",{{"mode","none"}}}})}}));
            Json pass=Json::array();const int hand=bodyIndex("L_Hand");for(auto [a,b]:enabledPairs(Json::object()))if(a!=hand&&b!=hand)pass.push_back({a,b});
            auto custom=physicsText(fitRig(*cached,{{"physicsOverrides",profile(Json::object(),{{"collisions",{{"mode","custom"},{"pairs",pass}}}})}}));
            check(count(basePhy,"\"collisionpair\"")==136&&enabledPairs(Json::object()).size()==136,"mode all writes the 136 legacy pairs");
            check(count(none,"\"collisionpair\"")==0&&count(none,"\"selfcollisions\" \"0\"")==1,"mode none writes only selfcollisions 0");
            check(count(custom,"\"collisionpair\"")==120&&custom.find("\""+std::to_string(hand)+",")==custom.npos,"a left-hand pass-through writes the other 120 pairs");
        }
        // N9: masses.
        {
            auto sum=[](const std::vector<float>& v){double s=0;for(auto x:v)s+=x;return s;};
            auto edited=fitRig(*cached,{{"physicsOverrides",profile(body("L_Hand",{{"damping",1}}))}});
            check(std::abs(sum(solidMasses(edited))-70)<1e-4,"weight shares add up to the total");
            auto light=fitRig(*cached,{{"mass",1},{"physicsOverrides",profile(body("L_Hand",{{"damping",1}}))}});auto masses=solidMasses(light);
            check(masses[bodyIndex("L_Hand")]==.1f&&masses[bodyIndex("Spine4")]>.1f,"edited profiles keep the 0.1 kg VPhysics floor");
            check(std::abs(solidMasses(fitRig(*cached,{{"mass",1}}))[bodyIndex("L_Hand")]-1.f/74)<1e-6,"unedited carriers keep the 2.2 masses without a floor");
            auto volume=fitRig(*cached,{{"physicsOverrides",profile(Json::object(),{{"massMode","volume"}})}});auto vm=solidMasses(volume);double weighted=0;
            for(size_t i=0;i<18;i++)weighted+=hullVolume(volume.bodies[i].hull,volume.manifest["bodies"][i]["faces"])*volume.bodies[i].massBias;
            bool studiomdl=true,floored=false;for(size_t i=0;i<18;i++){double expected=70.*hullVolume(volume.bodies[i].hull,volume.manifest["bodies"][i]["faces"])*volume.bodies[i].massBias/weighted;studiomdl&=std::abs(vm[i]-std::max(1.,expected))<1e-4;floored|=expected<1;}
            check(studiomdl&&floored&&physicsText(volume).find("\"totalmass\" \"70\"")!=std::string::npos,"volume weighting follows studiomdl with its 1 kg floor and keeps totalmass");
            auto automass=fitRig(*cached,{{"physicsOverrides",profile(Json::object(),{{"automass",{{"density",1000}}}})}});double total=0;for(size_t i=0;i<18;i++)total+=hullVolume(automass.bodies[i].hull,automass.manifest["bodies"][i]["faces"]);
            check(std::abs(automass.mass-std::clamp(total*1.6387064e-5*1000,1.,1000.))<1e-3&&std::abs(sum(solidMasses(automass))-automass.mass)<1e-3,"automass is the hull volume times the density");
            Json withMass=automass.manifest["bodies"];bool recorded=true;for(size_t i=0;i<18;i++)recorded&=std::abs(withMass[i]["mass"].get<double>()-solidMasses(automass)[i])<1e-6&&withMass[i]["volume"].get<double>()>0;
            check(recorded,"edited manifests record each body's mass and volume");
        }
        // N10: a friction-only profile changes only the friction lines.
        {
            auto rig=fitRig(*cached,{{"physicsOverrides",profile(body("L_Calf",{{"limits",{{"z",{-10,125,1.5}}}}}))}});auto a=lines(physicsText(rig)),b=lines(basePhy);
            bool only=a.size()==b.size();int changed=0;for(size_t i=0;only&&i<a.size();i++)if(a[i]!=b[i]){changed++;only&=a[i]=="\"zfriction\" \"1.5\""&&b[i]=="\"zfriction\" \"0\"";}
            check(only&&changed==1&&rig.manifest["physicsWriter"]==1&&rig.key!=base.key,"a friction-only profile differs from the legacy text in that line only");
        }
        // N11: box and capsule shapes.
        {
            const int forearm=bodyIndex("L_Forearm"),hand=bodyIndex("L_Hand");auto& fb=base.manifest["bodies"][forearm];
            Json overrides={{"ValveBiped.Bip01_L_Forearm",{{"center",fb["center"]},{"extent",fb["extent"]},{"style","capsule"}}},{"ValveBiped.Bip01_L_Hand",{{"style","box"}}}};
            auto closed=[](const Rig& r,int i){auto& faces=r.manifest["bodies"][i]["faces"];std::map<std::pair<int,int>,int> edges;for(auto& f:faces)for(size_t k=0;k<f.size();k++){int x=f[k],y=f[(k+1)%f.size()];edges[std::minmax(x,y)]++;}bool ok=faces.size()>=4;for(auto& [e,n]:edges)ok&=n==2;return ok;};
            for(bool path:{true,false}){
                Json options={{"collisionOverrides",overrides},{"collisionOverrideScale",base.scale}};if(!path)options["excludedMaterials"]=Json::array({999});
                auto rig=fitRig(*cached,options);auto& capsule=rig.bodies[forearm];auto& box=rig.bodies[hand];auto& cb=rig.manifest["bodies"][forearm];
                btVector3 c(fb["center"][0],fb["center"][1],fb["center"][2]),e(fb["extent"][0],fb["extent"][1],fb["extent"][2]),lo(1e9f,1e9f,1e9f),hi=-lo;for(auto v:capsule.hull){lo.setMin(v);hi.setMax(v);}
                int L=0;if(e[1]>e[L])L=1;if(e[2]>e[L])L=2;float r=std::min(e[(L+1)%3],e[(L+2)%3]),cap=std::min(e[L],r);double analytic=SIMD_PI*e[(L+1)%3]*e[(L+2)%3]*(2*(e[L]-cap)+4./3*cap);
                double v=hullVolume(capsule.hull,cb["faces"]);
                check(capsule.style=="capsule"&&cb["style"]=="capsule"&&capsule.hull.size()<=64&&closed(rig,forearm)&&(lo-(c-e)).length()<1e-4f&&(hi-(c+e)).length()<1e-4f,std::string("a capsule fills centre±half size with a closed hull of ≤64 points (")+(path?"cached":"full")+" fit)");
                check(std::abs(v/analytic-1)<.05,std::string("capsule volume is within 5% of the analytic capsule (")+std::to_string(v/analytic)+")");
                check(box.style=="box"&&box.hull.size()==8&&closed(rig,hand)&&rig.manifest["bodies"][hand]["style"]=="box",std::string("a box style is the 8 corners of the fitted box (")+(path?"cached":"full")+" fit)");
                check(!rig.manifest["bodies"][hand].value("overlapAdjusted",false)&&!rig.manifest["bodies"][forearm].value("overlapAdjusted",false)&&!rig.manifest["bodies"][0].contains("style"),"styled bodies keep their size and only styled bodies record a style");
                check(rig.key!=fitRig(*cached,{{"collisionOverrides",{{"ValveBiped.Bip01_L_Forearm",{{"center",fb["center"]},{"extent",fb["extent"]}}}}},{"collisionOverrideScale",base.scale}}).key,"a style changes the carrier key");
            }
            check(rejects([&]{fitRig(*cached,{{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",{{"style","sphere"}}}}}});})&&rejects([&]{fitRig(*full,{{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",{{"style",3}}}}}});})&&rejects([&]{primitiveHull("cylinder",{0,0,0},{1,1,1});}),"unknown styles are refused");
            check(fitRig(*cached,{{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",{{"style","fitted"}}}}}}).key==fitRig(*cached,{{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",Json::object()}}}}).key,"an explicit fitted style is the unstyled shape");
        }
        // N12: manifests round-trip every physics field; validateRig checks them.
        {
            auto rig=fitRig(*cached,{{"physicsOverrides",profile(body("Head1",{{"drag",2},{"surfaceprop","metal"},{"damping",1.25},{"limits",{{"x",{-40,40,0.4}}}}}),{{"massMode","volume"},{"collisions",{{"mode","none"}}}})},{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",{{"style","capsule"}}}}}});
            auto restored=rigFromManifest(rig.manifest);const int head=bodyIndex("Head1");auto& b=restored.bodies[head];
            check(restored.key==rig.key&&restored.physics==rig.physics&&b.drag==2&&b.surfaceprop=="metal"&&b.damping==1.25f&&b.friction.x()==.4f&&restored.bodies[bodyIndex("L_Hand")].style=="capsule","rigFromManifest restores the key, physics fields and styles");
            check(physicsText(restored)==physicsText(rig),"a restored rig writes the same .phy text");
            bool valid=!rejects([&]{validateRig(restored,*cached);});
            std::vector<std::pair<const char*,std::function<void(Json&)>>> mutations={
             {"NaN friction",[](Json& j){j["bodies"][3]["friction"]=Json::array({std::nan(""),0,0});}},
             {"negative damping",[](Json& j){j["bodies"][3]["damping"]=-1;}},
             {"negative inertia",[](Json& j){j["bodies"][3]["inertia"]=-2;}},
             {"negative drag",[](Json& j){j["bodies"][3]["drag"]=-0.5;}},
             {"surfaceprop with a space",[](Json& j){j["bodies"][3]["surfaceprop"]="A B";}}};
            for(auto& [label,mutate]:mutations){auto manifest=rig.manifest;mutate(manifest);check(rejects([&]{validateRig(rigFromManifest(manifest),*cached);}),std::string("validateRig rejects ")+label);}
            auto future=rig.manifest;future["physicsOverrides"]={{"schema",9},{"something",true}};
            check(valid&&!rejects([&]{validateRig(rigFromManifest(future),*cached);}),"validateRig never rejects a profile it cannot read: clients only render");
            auto old=base.manifest;check(!rejects([&]{validateRig(rigFromManifest(old),*cached);})&&rigFromManifest(old).physics.empty(),"2.2 manifests load with default physics fields");
        }
        // N13: the fit cache key.
        {
            Json options={{"scaleMultiplier",1.2},{"mass",60},{"collisionOverrides",Json::object()},{"role","ragdoll"},{"position",{1,2,3}}};
            check(carrierFitKey("abc",options)==legacyFitKey("abc",options),"without physicsOverrides the fit key is the 2.2 string");
            auto a=normalizeCarrierOptions({{"physicsOverrides",profile(body("L_Hand",{{"damping",2}}))}}),b=normalizeCarrierOptions({{"physicsOverrides",profile(body("L_Hand",{{"damping",3}}))}});
            check(carrierFitKey("abc",a)!=carrierFitKey("abc",b)&&carrierFitKey("abc",a)!=carrierFitKey("abc",Json::object()),"different profiles never share a fitted rig");
            check(normalizeCarrierOptions({{"physicsOverrides",Json::object()}})==Json::object()&&normalizeCarrierOptions({{"physicsOverrides",{{"schema",1}}}})==Json::object()&&normalizeCarrierOptions({{"role","arms"},{"physicsOverrides",{{"schema",7}}}})==Json({{"role","arms"}}),"empty profiles normalise to absent; c_arms drop them");
            check(rejects([&]{normalizeCarrierOptions({{"physicsOverrides",{{"schema",2}}}});}),"invalid profiles are refused before fitting");
        }
        // N14: canonicalisation is idempotent.
        {
            std::mt19937 random(5);auto uniform=[&](double lo,double hi){return std::uniform_real_distribution<double>(lo,hi)(random);};bool stable=true,valid=true;
            for(int n=0;n<200;n++){
                Json p=profile(Json::object());if(random()%2)p["surfaceprop"]=random()%2?"metal":"flesh";if(random()%3==0)p["massMode"]=random()%2?"volume":"bias";if(random()%4==0)p["automass"]={{"density",uniform(10,20000)}};
                if(random()%3==0){p["collisions"]={{"mode",random()%2?"none":"custom"}};if(p["collisions"]["mode"]=="custom"){Json pairs=Json::array();auto all=enabledPairs(Json::object());for(int k=0;k<20;k++){auto pick=all[random()%all.size()];pairs.push_back(random()%2?Json{pick.first,pick.second}:Json{pick.second,pick.first});}p["collisions"]["pairs"]=pairs;}}
                if(random()%4==0)p["animatedFriction"]={{"min",int(random()%100)},{"max",100+int(random()%900)},{"timeIn",uniform(0,10)},{"timeOut",uniform(0,10)},{"timeHold",uniform(0,10)}};
                for(int i=0;i<18;i++){if(random()%3)continue;Json f=Json::object();
                    if(i>0&&random()%2)for(int k=0;k<3;k++){if(random()%2)continue;double lo=uniform(-180,0),hi=uniform(0,180);int kind=int(random()%4);f["limits"][std::string(1,char('x'+k))]=kind==0?Json{0,0,uniform(0,100)}:kind==1?Json{-360,360,uniform(0,100)}:Json{lo,hi,uniform(0,100)};}
                    if(random()%2)f["massBias"]=uniform(.01,100);if(random()%2)f["damping"]=uniform(0,10);if(random()%2)f["rotdamping"]=uniform(0,100);if(random()%2)f["inertia"]=uniform(.1,100);if(random()%3==0)f["drag"]=uniform(0,100);if(random()%3==0)f["surfaceprop"]=random()%2?"wood":"flesh";
                    p["bodies"][CarrierBodyNames[i]]=f;}
                auto once=canonicalPhysics(p);auto twice=canonicalPhysics(once.value);
                valid&=once.errors.empty();stable&=twice.errors.empty()&&twice.value.dump()==once.value.dump();
            }
            check(valid&&stable,"canonicalPhysics(canonicalPhysics(x)) == canonicalPhysics(x) for 200 seeded profiles");
        }
        // N15: the editor's preview.
        {
            auto preview=previewCarrier(base);auto stature=(base.bones[base.bodies[3].bone].rest.getOrigin()-(base.bones[base.bodies[14].bone].rest.getOrigin()+base.bones[base.bodies[17].bone].rest.getOrigin())*.5f).length();
            bool shaped=preview["bodies"].size()==18;for(int i=0;i<18&&shaped;i++)shaped&=preview["bodies"][i]["index"]==i&&preview["bodies"][i]["name"]==CarrierBodyNames[i]&&preview["bodies"][i]["hull"].size()==base.bodies[i].hull.size()&&preview["bodies"][i]["drag"].is_null();
            check(preview["status"]=="ready"&&preview["key"]==base.key&&shaped&&preview["phyText"]==basePhy&&std::abs(preview["unit"].get<double>()-stature/60)<1e-5&&preview["pairs"]["count"]==136,"previewCarrier reports 18 bodies, the exact .phy text and the shape unit");
            auto& head=base.manifest["bodies"][3];Json bigger=Json::array();for(int k=0;k<3;k++)bigger.push_back(head["extent"][k].get<double>()*3);
            Json overrides={{"ValveBiped.Bip01_Head1",{{"center",head["center"]},{"extent",bigger}}}};
            auto big=previewCarrier(fitRig(*cached,{{"collisionOverrides",overrides},{"collisionOverrideScale",base.scale}}));bool found=false;
            for(auto& p:big["penetrations"])found|=p["a"]==3&&p["b"]==4&&p["depth"].get<double>()>.12*base.scale/ScmiSourceUnitsPerPmx;
            auto passing=previewCarrier(fitRig(*cached,{{"collisionOverrides",overrides},{"collisionOverrideScale",base.scale},{"physicsOverrides",profile(Json::object(),{{"collisions",{{"mode","none"}}}})}}));
            check(found&&passing["penetrations"].empty()&&passing["pairs"]["count"]==0&&passing["pairs"]["mode"]=="none","an enlarged head reports its overlap with the shoulder, unless collisions are off");
        }
        // N16: actor variants carry the same physics; c_arms ignore it.
        {
            auto reference=readAnimationModel(carrierFiles(base).at(base.path));reference["includes"]={"models/animation_fixture.mdl"};
            reference["ikChains"]=Json::array();for(auto side:{"R","L"})for(auto limb:{"hand","foot"}){Json links=Json::array();for(auto segment:std::string(limb)=="hand"?std::array{"UpperArm","Forearm","Hand"}:std::array{"Thigh","Calf","Foot"})links.push_back({{"bone",std::string("ValveBiped.Bip01_")+side+"_"+segment},{"knee",{0,-1,0}}});reference["ikChains"].push_back({{"name",std::string(side)+limb},{"type",0},{"links",links}});}
            reference["ikLocks"]={1,3};auto physics=profile(body("L_Forearm",{{"limits",{{"z",{-90,7.5,0.6}}}},{"damping",1}}),{{"collisions",{{"mode","none"}}}});
            auto ragdoll=physicsText(fitRig(*cached,{{"physicsOverrides",physics}}));
            for(auto role:{"citizen","combine","player"}){auto actor=fitRig(*cached,{{"role",role},{"gender","female"},{"animationSource","models/reference.mdl"},{"animationReference",reference},{"physicsOverrides",physics}});check(physicsText(actor)==ragdoll&&actor.manifest.contains("physicsOverrides"),std::string("the ")+role+" variant writes the ragdoll's physics");}
            Json arms={{"role","arms"},{"gender","female"},{"animationSource","models/reference.mdl"},{"animationReference",reference},{"armsParts",Json::array()}};auto plain=fitRig(*cached,arms);arms["physicsOverrides"]={{"schema",42}};
            bool ok=true;std::string key;try{key=fitRig(*cached,arms).key;}catch(const std::exception&){ok=false;}
            check(ok&&key==plain.key&&!plain.manifest.contains("physicsOverrides"),"c_arms ignore physicsOverrides, even an unreadable one");
        }
        // P1: the preview's fast path is cheap enough to follow every edit.
        {
            Json options={{"physicsOverrides",profile(body("L_Forearm",{{"damping",1.5}}))},{"collisionOverrides",{{"ValveBiped.Bip01_L_Hand",{{"style","capsule"}}}}}};std::vector<double> ms;
            for(int n=0;n<25;n++){auto started=std::chrono::steady_clock::now();auto preview=previewCarrier(fitRig(*cached,options));ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());}
            std::sort(ms.begin(),ms.end());std::cout<<"preview fast path: median "<<ms[12]<<" ms, p95 "<<ms[23]<<" ms\n";
            // The budget is 5 ms median / 8 ms p95 on a desktop; shared CI runners get headroom.
            check(ms[12]<25&&ms[23]<40,"the preview fast path stays interactive");
        }
        std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
