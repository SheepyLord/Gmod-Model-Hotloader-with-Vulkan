#include "core.hpp"
#include "texture_resolver.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

namespace props {
void applyMaterialOverrides(Asset& a,const fs::path& source,const Options& options){
    auto path=source;path+=L".gmodel.json";
    if(!fs::is_regular_file(path))return;
    const auto settings=readJson(path,1<<20);
    if(!settings.is_object()||settings.value("version",0)!=1)throw std::runtime_error("Material override file must be an object using version 1");
    for(auto it=settings.begin();it!=settings.end();++it)if(it.key()!="version"&&it.key()!="materials"&&it.key()!="meshes")throw std::runtime_error("Unknown material override section: "+it.key());
    for(auto section:{"materials","meshes"})if(settings.contains(section)){
        const auto& entries=settings.at(section);
        if(!entries.is_object()||entries.size()>128)throw std::runtime_error("Material override section must be an object with at most 128 names");
        for(auto it=entries.begin();it!=entries.end();++it){
            const auto& entry=it.value();if(!entry.is_object())throw std::runtime_error("Material override must be an object: "+it.key());
            for(auto field=entry.begin();field!=entry.end();++field){
                const auto& key=field.key();const auto& v=field.value();bool valid=false;
                auto number=[](const Json& n,double max){return n.is_number()&&std::isfinite(n.get<double>())&&n.get<double>()>=0&&n.get<double>()<=max;};
                if(key=="color"||key=="specular")valid=v.is_array()&&v.size()==(key=="color"?4:3)&&std::all_of(v.begin(),v.end(),[&](const Json& n){return number(n,1);});
                else if(key=="two_sided"||key=="unlit"||key=="white_opacity")valid=v.is_boolean();
                else if(key=="alpha_mode")valid=v=="opaque"||v=="mask"||v=="blend";
                else if(key=="alpha_cutoff"||key=="shininess")valid=number(v,key=="shininess"?1000:1);
                else if(key=="base_texture"||key=="normal_texture"||key=="opacity_texture")valid=v.is_string()&&!v.get_ref<const std::string&>().empty()&&v.get_ref<const std::string&>().size()<=32768;
                if(!valid)throw std::runtime_error("Invalid material override field: "+it.key()+" / "+key);
            }
            if((entry.contains("opacity_texture")&&!entry.contains("base_texture"))||(entry.contains("white_opacity")&&!entry.contains("opacity_texture")))throw std::runtime_error("Opacity override requires a base texture and opacity texture");
        }
    }
    TextureResolver resources(source.parent_path());
    auto& materials=a.manifest.at("materials");unsigned applied=0;
    auto& warnings=a.manifest["warnings"];if(warnings.is_null())warnings=Json::array();
    std::set<std::string> matchedMaterials,matchedMeshes;
    auto apply=[&](Json& m,const Json& override){
        if(!override.is_object())throw std::runtime_error("Material override must be an object");
        for(auto key:{"color","two_sided","alpha_mode","alpha_cutoff","specular","shininess","unlit"})if(override.contains(key))m[key]=override[key];
        if(override.contains("alpha_mode"))m["alpha_explicit"]=true;
        if(override.contains("specular")||override.contains("shininess"))m.erase("pbr"); // explicit Phong wins
        for(auto binding:{"base_texture","normal_texture"})if(override.contains(binding)){
            auto ref=override.at(binding).get<std::string>();
            auto bytes=readFile(resources.resolve(ref).path,128ull<<20);
            Texture texture;
            if(std::string_view(binding)=="base_texture"&&override.contains("opacity_texture")){
                auto mask=readFile(resources.resolve(override.at("opacity_texture").get<std::string>()).path,128ull<<20);
                texture=makeMaskedTexture(bytes,mask,options.limits.textureDimension,override.value("white_opacity",false));
            }else texture=makeTexture(bytes,options.limits.textureDimension);
            auto hash=texture.hash;addTexture(a,std::move(texture));m[binding]=hash;
            // Resolve only this binding's diagnostic; a repaired diffuse map
            // must not conceal an independently missing normal map.
            if(m.contains("texture_errors")){
                auto& errors=m["texture_errors"];
                if(errors.contains(binding)){
                    const auto message=errors.at(binding);
                    warnings.erase(std::remove(warnings.begin(),warnings.end(),message),warnings.end());
                    errors.erase(binding);
                }
                if(errors.empty()){m.erase("texture_errors");m.erase("missing_texture");}
            }
        }
        ++applied;
    };
    const auto named=settings.value("materials",Json::object());
    for(auto& m:materials){auto name=m.value("name",std::string{});if(named.contains(name)){apply(m,named.at(name));matchedMaterials.insert(name);}}
    const auto meshes=settings.value("meshes",Json::object());std::map<std::pair<std::string,size_t>,size_t> assigned;
    for(auto& part:a.manifest.at("parts")){
        auto mesh=part.value("mesh",std::string{});if(!meshes.contains(mesh))continue;
        matchedMeshes.insert(mesh);
        auto old=part.at("material").get<size_t>();auto key=std::pair{mesh,old};auto found=assigned.find(key);
        if(found==assigned.end()){
            if(materials.size()>=options.limits.materials)throw std::runtime_error("Material overrides exceed the material budget");
            Json m=materials.at(old);m["name"]=mesh;m["source_order"]=materials.size();apply(m,meshes.at(mesh));
            size_t index=materials.size();materials.push_back(std::move(m));assigned[key]=index;part["material"]=index;
        }else part["material"]=found->second;
    }
    for(auto it=named.begin();it!=named.end();++it)if(!matchedMaterials.contains(it.key()))warnings.push_back("Material override did not match a material: "+it.key());
    for(auto it=meshes.begin();it!=meshes.end();++it)if(!matchedMeshes.contains(it.key()))warnings.push_back("Material override did not match a mesh: "+it.key());
    // Replaced images must not inflate cache/transfer size or consume the memory
    // budget. Only normalized content hashes leave this local import process.
    std::set<std::string> used;
    for(const auto& m:materials)for(auto binding:{"base_texture","normal_texture"})used.insert(m.value(binding,std::string{}));
    std::erase_if(a.textures,[&](const Texture& t){return !used.contains(t.hash);});
    a.manifest["material_overrides_applied"]=applied;
}
}
