#include "core.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "../test_platform.hpp"
using namespace props;
static int checks=0;
void check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
template<class F>void rejects(F f,const char* msg){bool threw=false;try{f();}catch(...){threw=true;}check(threw,msg);}
Asset fixture(){
    Asset a;
    Hull h{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
      {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,2,3,7,2,7,6,0,4,7,0,7,3,1,2,6,1,6,5}};
    a.hulls={h}; for(auto p:h.points){Vertex v;v.pos=p;v.normal=p.normalized();a.vertices.push_back(v);}
    a.indices=h.indices;
    a.manifest={{"version",1},{"name","Cube"},{"materials",Json::array({{{"color",{1,1,1,1}},{"alpha_mode","opaque"}}})},
      {"parts",Json::array({{{"first",0},{"count",36},{"material",0}}})}};
    updateBounds(a);return a;
}
#include "material_processing_tests.hpp"
#include "fast_hull_tests.hpp"
#include "texture_resolver_tests.hpp"
#include "blend_tests.hpp"
int main(){try{
    auto temporary=fs::temp_directory_path()/fs::path(L"gmodel-atomic-test-"+std::to_wstring(processId()));
    fs::create_directories(temporary);auto statusPath=temporary/L"status.json";
    writeJson(statusPath,{{"sequence",0},{"payload",std::string(2048,'x')}});
    std::atomic_bool done=false;std::exception_ptr writeError;
    std::thread writer([&]{try{for(unsigned i=0;i<150;++i)writeJson(statusPath,{{"sequence",i},{"payload",std::string(2048,'x')}});}catch(...){writeError=std::current_exception();}done=true;});
    unsigned snapshots=0;std::exception_ptr readError;
    try{do{auto value=readJson(statusPath);if(value.at("payload").get<std::string>()!=std::string(2048,'x'))throw std::runtime_error("Partial atomic snapshot");++snapshots;}while(!done);}catch(...){readError=std::current_exception();}
    writer.join();fs::remove_all(temporary);
    if(writeError){try{std::rethrow_exception(writeError);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}}
    check(!writeError,"concurrent atomic write failed");check(!readError&&snapshots>0,"concurrent status polling failed");
    auto a=fixture();validate(a);auto b=encode(a);auto c=decode(b);
    check(c.vertices.size()==8&&c.indices.size()==36,"cache round trip");
    check(validHash(c.id)&&sha256(b)==c.id,"content hash");
    for(size_t i=0;i<b.size();i+=19)rejects([&]{decode(std::span(b).first(i));},"truncation accepted");
    auto bad=a;bad.indices[0]=99;rejects([&]{validate(bad);},"invalid index accepted");
    bad=a;bad.vertices[0].pos.x=NAN;rejects([&]{validate(bad);},"NaN accepted");
    bad=a;bad.manifest["parts"][0]["first"]=3;rejects([&]{validate(bad);},"bad range accepted");
    Limits small;small.triangleWarning=1;check(decode(b,small).indices.size()==36,"geometry warning threshold rejected a valid cache");
    auto warning=a.manifest;warnLargeGeometry(warning,a.vertices.size(),a.indices.size(),small);
    check(warning.contains("geometry_warning")&&warning["warnings"].size()==1,"dense geometry warning missing");
    warnLargeGeometry(warning,a.vertices.size(),a.indices.size(),small);check(warning["warnings"].size()==1,"geometry warning duplicated");
    small.packageBytes=100;rejects([&]{decode(b,small);},"byte budget was removed with the geometry cap");
    rejects([]{checkGeometryStorage(UINT32_MAX,UINT32_MAX);},"unsafe geometry allocation accepted");
    {
        auto dense=a;dense.indices.clear();dense.indices.reserve(3000024);
        for(unsigned i=0;i<83334;++i)dense.indices.insert(dense.indices.end(),a.indices.begin(),a.indices.end());
        dense.manifest["parts"]=Json::array();
        for(size_t first=0;first<dense.indices.size();first+=60000)dense.manifest["parts"].push_back({{"first",first},{"count",std::min<size_t>(60000,dense.indices.size()-first)},{"material",0}});
        warnLargeGeometry(dense.manifest,dense.vertices.size(),dense.indices.size());
        auto loaded=decode(encode(dense));prepareRenderPlan(loaded);
        check(loaded.indices.size()/3>1000000&&loaded.manifest.contains("geometry_warning"),"million-triangle cache still rejected or warning absent");
        bool smallChunks=true;for(auto& chunk:loaded.renderChunks)smallChunks&=chunk["count"].get<size_t>()<=18000;
        check(smallChunks,"large asset bypassed bounded render uploads");
    }
    b.back()^=0xFF;rejects([&]{decode(b);},"corrupt index accepted");
    auto hit=trace(a.hulls,{-4,0,0},{8,0,0});check(hit.hit&&std::abs(hit.fraction-.375f)<1e-5,"ray cube hit");
    hit=trace(a.hulls,{-4,3,0},{8,0,0});check(!hit.hit,"ray cube miss");
    hit=trace(a.hulls,{-4,0,0},{8,0,0},{{{.5,0,0},{0,.5,0},{0,0,.5}}});
    check(hit.hit&&std::abs(hit.fraction-.3125f)<1e-5,"swept box hit");
    bad=a;bad.hulls[0].indices.resize(33);rejects([&]{validate(bad);},"open hull accepted");
    bad=a;bad.hulls[0].points[0]={0,0,0};rejects([&]{validate(bad);},"concave hull accepted");
    bad=a;Texture malformed;malformed.width=2;malformed.height=2;malformed.png.resize(33);
    unsigned char sig[]={137,80,78,71,13,10,26,10};std::copy(sig,sig+8,malformed.png.begin());
    malformed.png[12]='I';malformed.png[13]='H';malformed.png[14]='D';malformed.png[15]='R';malformed.png[19]=2;malformed.png[23]=2;
    malformed.hash=sha256(malformed.png);bad.textures.push_back(malformed);
    rejects([&]{validate(bad);},"truncated PNG accepted");
    auto header=encode(a);for(int i=12;i<16;i++)header[i]=255;
    rejects([&]{decode(header);},"oversized allocation header accepted");
    // What saveAsset writes always loads again: a manifest beyond what the reader takes (many
    // mesh parts with long names) is refused before anything is written, and a large one within it loads.
    {auto root=fs::temp_directory_path()/fs::path(L"gmodel-manifest-test-"+std::to_wstring(processId()));fs::remove_all(root);fs::create_directories(root/L"assets");
     auto big=a;big.manifest["parts"][0]["name"]=std::string(ManifestBytes,'n');
     bool refused=false;try{saveAsset(root,big);}catch(const std::exception& e){refused=std::string(e.what()).find("4 MiB")!=std::string::npos;}
     check(refused&&fs::is_empty(root/L"assets"),"a bundle whose manifest the reader refuses was saved");
     auto fits=a;auto room=ManifestBytes-encode(a).size();fits.manifest["parts"][0]["name"]=std::string(room/2,'n');
     auto id=saveAsset(root,fits);check(loadAsset(root,id).indices.size()==36,"a saved bundle does not load again");
     auto many=a;Limits one;one.materials=1;
     for(int i=0;i<3;i++){Texture t;t.hash=std::string(63,'0')+char('1'+i);many.textures.push_back(t);}
     rejects([&]{validate(many,one);},"more textures than the reader takes were accepted");
     fs::remove_all(root);}
    check(wide(utf8(L"模型 folder"))==L"模型 folder","Unicode roundtrip");
    rejects([]{parseOptions({{"scale",0}});},"zero scale accepted");
    rejects([]{parseOptions({{"axis","invalid"}});},"unknown axis accepted");
    rejects([]{parseHulls(Json::array());},"empty collider accepted");
    materialProcessingTests();
    fastHullTests();
    textureResolverTests();
    blendTests();
    auto render=a;render.manifest["materials"][0]["two_sided"]=true;
    auto originalBytes=encode(render);prepareRenderPlan(render);
    check(encode(render)==originalBytes,"render preparation changed package identity");
    check(render.renderChunks.size()==2&&render.renderIndices.size()==a.indices.size(),"two-sided render chunks");
    bool sourceWinding=true;
    for(size_t i=0;i<a.indices.size();i+=3)sourceWinding&=render.renderIndices[i]==a.indices[i]&&render.renderIndices[i+1]==a.indices[i+2]&&render.renderIndices[i+2]==a.indices[i+1];
    check(sourceWinding&&render.renderManifest["render_winding"]=="source_cw","Source clockwise render winding");
    auto preparedIndices=render.renderIndices;prepareRenderPlan(render);
    check(render.renderIndices==preparedIndices&&encode(render)==originalBytes,"repeated render preparation reversed winding or changed cache");
    auto shifted=a;for(auto& v:shifted.vertices)v.pos=v.pos+Vec{13,-24,8};updateBounds(shifted);prepareRenderPlan(shifted);
    check((shifted.renderOrigin-Vec{13,-24,8}).length()<1e-6f&&(renderVertex(shifted,0).pos+shifted.renderOrigin-shifted.vertices[shifted.indices[0]].pos).length()<1e-6f,"centered render geometry moves source origin");
    auto front=renderVertex(render,1),back=renderVertex(render,2,true);
    check((front.pos-back.pos).length()<1e-6f&&(front.normal+back.normal).length()<1e-6f&&front.tangent[3]==-back.tangent[3],"backface winding/normal/tangent");
    auto glass=a;glass.manifest["materials"][0]["alpha_explicit"]=true;glass.manifest["materials"][0]["alpha_mode"]="blend";
    glass.indices.clear();for(int i=0;i<150;++i)glass.indices.insert(glass.indices.end(),a.indices.begin(),a.indices.end());
    glass.manifest["parts"][0]["count"]=glass.indices.size();prepareRenderPlan(glass);
    auto sorted=glass.renderIndices,expectedIndices=glass.indices;std::sort(sorted.begin(),sorted.end());std::sort(expectedIndices.begin(),expectedIndices.end());
    check(sorted==expectedIndices&&glass.renderChunks.size()>1,"blend clusters lost geometry");
    bool bounded=true;for(auto& chunk:glass.renderChunks)bounded&=chunk["count"].get<size_t>()<=384;
    check(bounded,"blend clusters exceed vertex target");
    hit=traceScaled(a.hulls,{-4,0,0},{8,0,0},{},2);check(hit.hit&&std::abs(hit.fraction-.25f)<1e-5,"scaled ray collision");
    hit=traceScaled(a.hulls,{-4,0,0},{8,0,0},{{{.5,0,0},{0,.5,0},{0,0,.5}}},2);check(hit.hit&&std::abs(hit.fraction-.1875f)<1e-5,"scaled swept box collision");
    rejects([]{propScale(NAN);},"NaN prop scale accepted");rejects([]{propScale(0);},"zero prop scale accepted");rejects([]{propScale(100.01);},"oversize prop scale accepted");
    check(propScale(double(.01f))==.01f,"network float scale endpoint");
    auto enormous=a.hulls;enormous[0].points[0].x=10000;rejects([&]{validateHullScale(enormous,100);},"unsafe scaled collider accepted");
    // Deletion beside damaged bundles: manifests with the wrong types (materials holding a
    // number, a texture reference that is not a string) block no deletion, their own
    // included, and keep every texture while their references are unknown.
    {auto root=fs::temp_directory_path()/fs::path(L"gmodel-delete-test-"+std::to_wstring(processId()));fs::remove_all(root);
     fs::create_directories(root/L"assets");fs::create_directories(root/L"textures");
     auto bundle=[&](char c,const Json& manifest){std::string id(64,c);auto text=manifest.dump();std::string bytes("GMLHOT1",8);
      for(int k=0;k<4;k++)bytes.push_back(char(text.size()>>(8*k)));bytes.append(12,'\0');bytes+=text;
      std::ofstream(root/L"assets"/wide(id+".gmdl"),std::ios::binary)<<bytes;return id;};
     std::string t1(64,'1'),t2(64,'2');for(auto& t:{t1,t2})std::ofstream(root/L"textures"/wide(t+".png"))<<"png";
     auto first=bundle('a',{{"materials",{{{"base_texture",t1}}}}}),second=bundle('b',{{"materials",{{{"base_texture",t1},{"normal_texture",t2}}}}});
     auto numbers=bundle('c',{{"materials",{1}}}),typed=bundle('d',{{"materials",{{{"base_texture",7}}}}});
     auto exists=[&](const std::string& id,const wchar_t* folder,const char* extension){return fs::exists(root/folder/wide(id+extension));};
     check(bundleManifest(root/L"assets"/wide(first+".gmdl"))["materials"][0]["base_texture"]==t1,"hand-written bundle header unreadable");
     Json result;try{result=deleteBundles(root,{first});}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}
     check(!result.is_null()&&!exists(first,L"assets",".gmdl")&&exists(t1,L"textures",".png")&&exists(t2,L"textures",".png"),"a damaged bundle blocked deleting another prop or lost a texture");
     result=Json();try{result=deleteBundles(root,{numbers,typed});}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}
     check(!result.is_null()&&!exists(numbers,L"assets",".gmdl")&&!exists(typed,L"assets",".gmdl"),"a damaged bundle could not be deleted");
     deleteBundles(root,{second});
     check(!exists(second,L"assets",".gmdl")&&!exists(t1,L"textures",".png")&&!exists(t2,L"textures",".png"),"deleting the last reference kept its textures");
     fs::remove_all(root);}
    std::cout<<checks<<" native checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
