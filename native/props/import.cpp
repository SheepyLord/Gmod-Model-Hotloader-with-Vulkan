#include "core.hpp"
#include "material_processing.hpp"
#include "texture_resolver.hpp"
#include <assimp/Importer.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/config.h>
#include <assimp/GltfMaterial.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <cstdlib>
#include <climits>
#include <memory>
#include <zlib.h>
#include <assimp/ProgressHandler.hpp>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>
static unsigned char* fastPngDeflate(unsigned char* data,int length,int* outputLength,int quality){
    uLongf size=compressBound(uLong(length));auto out=static_cast<unsigned char*>(std::malloc(size));
    if(!out)return nullptr;
    if(compress2(out,&size,data,uLong(length),quality)!=Z_OK||size>INT_MAX){std::free(out);return nullptr;}
    *outputLength=int(size);return out;
}
#define STBIW_ZLIB_COMPRESS fastPngDeflate
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include <stb_image_resize2.h>

namespace props {
class MemoryStream final:public Assimp::IOStream {
    Bytes bytes;size_t pos=0;
public:
    explicit MemoryStream(Bytes b):bytes(std::move(b)){}
    size_t Read(void* out,size_t size,size_t count)override{if(!size)return 0;count=std::min(count,(bytes.size()-pos)/size);std::memcpy(out,bytes.data()+pos,count*size);pos+=count*size;return count;}
    size_t Write(const void*,size_t,size_t)override{return 0;}
    aiReturn Seek(size_t offset,aiOrigin origin)override{size_t next=offset;if(origin==aiOrigin_CUR){if(offset>bytes.size()-pos)return aiReturn_FAILURE;next=pos+offset;}if(origin==aiOrigin_END){if(offset>bytes.size())return aiReturn_FAILURE;next=bytes.size()-offset;}if(next>bytes.size())return aiReturn_FAILURE;pos=next;return aiReturn_SUCCESS;}
    size_t Tell()const override{return pos;}size_t FileSize()const override{return bytes.size();}void Flush()override{}
};
// The model file, and what it names (glTF buffers, OBJ material libraries) only from its
// own folder and below: never an absolute path or one that climbs out, a link out of it
// or a denied place (DependencyScope).
class UnicodeIO final:public Assimp::IOSystem {
    fs::path model;DependencyScope scope;
    fs::path path(const char* p)const{std::string s(p);if(networkPath(s))throw std::runtime_error("Network model dependencies are not supported");
        auto q=fs::absolute(fs::path(wide(s))).lexically_normal();if(q==model)return q;
        q=scope.locate(s);if(q.empty()||!scope.allows(q))throw std::runtime_error("Model dependencies are read only from the model's own folder");return q;}
public:
    explicit UnicodeIO(const fs::path& source):model(fs::absolute(source).lexically_normal()),scope(model.parent_path(),false){}
    bool Exists(const char* p)const override{try{return fs::is_regular_file(path(p));}catch(...){return false;}}
    char getOsSeparator()const override{return '/';}
    Assimp::IOStream* Open(const char* p,const char* mode="rb")override{try{if(std::strchr(mode,'w')||std::strchr(mode,'a'))return nullptr;return new MemoryStream(readFile(path(p)));}catch(...){return nullptr;}}
    void Close(Assimp::IOStream* s)override{delete s;}
};
// Images larger than the limit are scaled down to it, keeping the ratio: the
// game shows no more detail. Linear filtering suits colour and normal maps alike.
static Texture encodeTexture(const uint8_t* rgba,int w,int h,uint32_t limit){
    stbi_write_png_compression_level=1; // Lossless fast encoding; pixels/alpha are unchanged.
    stbi_write_force_png_filter=1; // Sub filter avoids five full-image filter trials.
    Texture t;for(size_t i=3;i<size_t(w)*h*4;i+=4){t.hasAlpha|=rgba[i]<255;t.fractionalAlpha|=rgba[i]>0&&rgba[i]<255;}
    t.alphaKnown=true;Bytes scaled;
    if(uint32_t(w)>limit||uint32_t(h)>limit){
        double factor=double(limit)/std::max(w,h);int sw=std::clamp(int(std::lround(w*factor)),1,int(limit)),sh=std::clamp(int(std::lround(h*factor)),1,int(limit));
        scaled.resize(size_t(sw)*sh*4);if(!stbir_resize_uint8_linear(rgba,w,h,0,scaled.data(),sw,sh,0,STBIR_RGBA))throw std::runtime_error("Cannot scale down a large texture");
        t.sourceWidth=uint32_t(w);t.sourceHeight=uint32_t(h);rgba=scaled.data();w=sw;h=sh;
    }
    t.width=w;t.height=h;
    auto output=[](void* p,void* data,int size){auto& b=*static_cast<Bytes*>(p);auto q=static_cast<uint8_t*>(data);b.insert(b.end(),q,q+size);};
    if(!stbi_write_png_to_func(output,&t.png,w,h,4,rgba,w*4))throw std::runtime_error("Texture conversion failed");t.hash=sha256(t.png);return t;
}
Texture makeTexture(std::span<const uint8_t> input,uint32_t limit){
    int w=0,h=0,c=0;if(input.size()>MaxTextureFileBytes||!stbi_info_from_memory(input.data(),int(input.size()),&w,&h,&c)||w<=0||h<=0)throw std::runtime_error("Unsupported texture or texture exceeds 16384 pixels");
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(input.data(),int(input.size()),&w,&h,&c,4),stbi_image_free);if(!pixels)throw std::runtime_error("Texture decoding failed");
    return encodeTexture(pixels.get(),w,h,limit);
}
Texture makeMaskedTexture(std::span<const uint8_t> base,std::span<const uint8_t> mask,uint32_t limit,bool white){
    auto decode=[&](std::span<const uint8_t> data,int& w,int& h){int c=0;
        if(data.size()>MaxTextureFileBytes||!stbi_info_from_memory(data.data(),int(data.size()),&w,&h,&c)||w<=0||h<=0)throw std::runtime_error("Opacity image exceeds supported dimensions");
        std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> result(stbi_load_from_memory(data.data(),int(data.size()),&w,&h,&c,4),stbi_image_free);
        if(!result)throw std::runtime_error("Cannot decode opacity image");return result;};
    int w,h,mw,mh;auto rgba=decode(base,w,h),alpha=decode(mask,mw,mh);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){size_t i=(size_t(y)*w+x)*4,mi=(size_t(y)*mh/h*mw+size_t(x)*mw/w)*4;
        rgba.get()[i+3]=uint8_t(unsigned(rgba.get()[i+3])*alpha.get()[mi]/255);
        if(white)rgba.get()[i]=rgba.get()[i+1]=rgba.get()[i+2]=255;
    }
    return encodeTexture(rgba.get(),w,h,limit);
}
void addTexture(Asset& a,Texture t,const std::string& name){for(auto& e:a.textures)if(e.hash==t.hash)return;
    if(t.sourceWidth)a.manifest["warnings"].push_back("Large texture "+(name.empty()?std::string():name+" ")+"("+std::to_string(t.sourceWidth)+" x "+std::to_string(t.sourceHeight)+") was scaled down to "+std::to_string(t.width)+" x "+std::to_string(t.height)+"; the game shows no more detail than that.");
    a.textures.push_back(std::move(t));}
static Json material(){return {{"alpha_explicit",false},{"specular",{0,0,0}},{"shininess",1},{"name","Material"},{"color",{1,1,1,1}},{"two_sided",false},{"alpha_mode","opaque"},{"alpha_cutoff",0.5},{"base_texture",""},{"normal_texture",""}};}
static Texture rawTexture(const aiTexture* t,uint32_t limit){
    if(!t->mHeight)return makeTexture({reinterpret_cast<const uint8_t*>(t->pcData),t->mWidth},limit);
    if(t->mWidth>unsigned(STBI_MAX_DIMENSIONS)||t->mHeight>unsigned(STBI_MAX_DIMENSIONS))throw std::runtime_error("Embedded texture exceeds 16384 pixels");
    Bytes rgba(size_t(t->mWidth)*t->mHeight*4);for(size_t i=0;i<rgba.size()/4;i++){auto p=t->pcData[i];rgba[i*4]=p.r;rgba[i*4+1]=p.g;rgba[i*4+2]=p.b;rgba[i*4+3]=p.a;}
    return encodeTexture(rgba.data(),int(t->mWidth),int(t->mHeight),limit);
}
Asset importModel(const fs::path& source,const Options& options,const Progress& progress){
    auto ext=utf8(source.extension().wstring());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext==".pmx"){auto a=importPMX(source,options,progress);finishImport(a,source,options,progress);return a;}
    if(ext==".blend"){auto a=importBlend(source,options,progress);finishImport(a,source,options,progress);return a;}
    if(ext!=".obj"&&ext!=".fbx"&&ext!=".glb"&&ext!=".gltf")throw std::runtime_error("Choose OBJ, FBX, GLB, GLTF, PMX or BLEND");
    progress("Reading geometry",.02f,{},0,0,true);Assimp::Importer imp;
    class ParseProgress final:public Assimp::ProgressHandler { const Progress& progress; float last=-1;
    public:explicit ParseProgress(const Progress& p):progress(p){} bool Update(float percent)override{
        if(percent<0||percent-last>=.05f){last=percent;progress("Reading geometry",.02f+std::max(0.f,percent)*.08f,{},uint64_t(std::max(0.f,percent)*100),100,percent<0);}return true;
    }};imp.SetProgressHandler(new ParseProgress(progress));imp.SetIOHandler(new UnicodeIO(source));imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS,true);imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION,true);
    unsigned flags=aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_GenSmoothNormals|aiProcess_CalcTangentSpace|aiProcess_ValidateDataStructure|aiProcess_SortByPType;
    // Normalize the pinned glTF importer's sparse specular-color slot before
    // validation, then still run the full validation/postprocess pipeline.
    auto scene=imp.ReadFile(utf8(source.wstring()),0);
    if(!scene||!scene->mRootNode)throw std::runtime_error(std::string("Model parser: ")+imp.GetErrorString());
    if(scene->mNumMaterials>options.limits.materials)throw std::runtime_error("Material limit exceeded");
    if((ext==".glb"||ext==".gltf")&&scene->mMaterials){
        for(unsigned i=0;i<scene->mNumMaterials;++i){auto m=scene->mMaterials[i];
            if(!m||!m->mProperties||m->mNumProperties>65536)continue;
            aiString zero,one;
            unsigned specularFiles=0;for(unsigned k=0;k<m->mNumProperties;++k){auto p=m->mProperties[k];if(p&&p->mSemantic==aiTextureType_SPECULAR&&std::strcmp(p->mKey.C_Str(),"$tex.file")==0)++specularFiles;}
            if(specularFiles==1&&m->GetTexture(aiTextureType_SPECULAR,0,&zero)!=AI_SUCCESS&&m->GetTexture(aiTextureType_SPECULAR,1,&one)==AI_SUCCESS)
                for(unsigned k=0;k<m->mNumProperties;++k){auto p=m->mProperties[k];if(p&&p->mSemantic==aiTextureType_SPECULAR&&p->mIndex==1)p->mIndex=0;}
        }
    }
    // A bone listed twice in one mesh fails Assimp's validation. The FBX importer
    // can even give a mesh the same bone object twice (and would delete it
    // twice). Bones only describe the skeleton of a prop: drop repeated objects
    // and rename repeated names.
    for(unsigned i=0;i<scene->mNumMeshes;++i){auto mesh=scene->mMeshes[i];if(!mesh||!mesh->mBones)continue;std::set<const aiBone*> listed;std::set<std::string> seen;unsigned kept=0;
        for(unsigned b=0;b<mesh->mNumBones;++b){auto bone=mesh->mBones[b];if(!bone||!listed.insert(bone).second)continue;mesh->mBones[kept++]=bone;
            std::string n=bone->mName.C_Str();if(seen.insert(n).second)continue;
            for(int k=2;;++k){auto renamed=n+"#"+std::to_string(k);if(seen.insert(renamed).second){bone->mName.Set(renamed);break;}}}
        mesh->mNumBones=kept;}
    scene=imp.ApplyPostProcessing(flags);
    if(!scene||!scene->mRootNode)throw std::runtime_error(std::string("Model parser: ")+imp.GetErrorString());
    uint64_t sceneTriangles=0,sceneVertices=0;
    std::function<void(const aiNode*,unsigned)> preflight=[&](const aiNode* node,unsigned depth){
        if(depth>256)throw std::runtime_error("Scene hierarchy is too deep");
        for(unsigned i=0;i<node->mNumMeshes;++i){auto mesh=scene->mMeshes[node->mMeshes[i]];
            if(!(mesh->mPrimitiveTypes&aiPrimitiveType_TRIANGLE))continue;
            sceneTriangles+=mesh->mNumFaces;sceneVertices+=mesh->mNumVertices;
            checkGeometryStorage(sceneVertices,sceneTriangles*3,options.limits);
        }
        for(unsigned i=0;i<node->mNumChildren;++i)preflight(node->mChildren[i],depth+1);
    };
    preflight(scene->mRootNode,0); // Bound allocations before image work; counts alone do not reject.
    Asset a;a.manifest={{"version",1},{"name",utf8(source.stem().wstring())},{"format",ext.substr(1)},{"materials",Json::array()},{"parts",Json::array()},{"warnings",Json::array()}};
    if(scene->mNumMaterials>options.limits.materials)throw std::runtime_error("Material limit exceeded");
    if(scene->mNumAnimations)a.manifest["warnings"].push_back("Animations were ignored; the undeformed model is imported.");
    {
        // Rigged humanoids (Mixamo, VRChat, game rips) import as a statue in
        // their bind pose; record it so the game can say so.
        std::set<std::string> bones;
        for(unsigned i=0;i<scene->mNumMeshes;++i)for(unsigned b=0;b<scene->mMeshes[i]->mNumBones;++b){std::string n=scene->mMeshes[i]->mBones[b]->mName.C_Str();for(auto& c:n)c=char(std::tolower((unsigned char)c));bones.insert(n);}
        static const std::vector<std::vector<std::string>> parts={{"head"},{"neck"},{"spine","chest"},{"hip","pelvis"},{"arm"},{"hand"},{"leg","thigh","calf","knee"},{"foot"}};
        size_t found=0;for(auto& group:parts){bool hit=false;for(auto& n:bones){for(auto& token:group)if(n.find(token)!=n.npos){hit=true;break;}if(hit)break;}found+=hit;}
        if(!bones.empty())a.manifest["skeleton"]={{"bones",bones.size()},{"humanoid",found>=6&&bones.size()>=15}};
    }
    std::map<std::string,size_t> convertedTextures;
    TextureResolver resources(source.parent_path());unsigned repairedTextures=0;
    for(unsigned i=0;i<scene->mNumMaterials;i++){
        progress("Converting textures",.10f+.30f*float(i)/std::max(1u,scene->mNumMaterials),"Material "+std::to_string(i+1)+" of "+std::to_string(scene->mNumMaterials),i,scene->mNumMaterials);
        auto m=scene->mMaterials[i];Json j=material();aiString name;m->Get(AI_MATKEY_NAME,name);j["name"]=name.C_Str();j["source_order"]=i;j["alpha_explicit"]=ext==".glb"||ext==".gltf";aiColor3D specular(0,0,0);m->Get(AI_MATKEY_COLOR_SPECULAR,specular);j["specular"]={std::clamp(specular.r,0.f,1.f),std::clamp(specular.g,0.f,1.f),std::clamp(specular.b,0.f,1.f)};float shininess=1;m->Get(AI_MATKEY_SHININESS,shininess);j["shininess"]=std::clamp(shininess,0.f,1000.f);aiColor4D c(1,1,1,1);if(m->Get(AI_MATKEY_BASE_COLOR,c)!=AI_SUCCESS)m->Get(AI_MATKEY_COLOR_DIFFUSE,c);float opacity=1;m->Get(AI_MATKEY_OPACITY,opacity);if(ext==".glb"||ext==".gltf")opacity=1;j["color"]={std::clamp(c.r,0.f,1.f),std::clamp(c.g,0.f,1.f),std::clamp(c.b,0.f,1.f),std::clamp(c.a*opacity,0.f,1.f)};int two=0;m->Get(AI_MATKEY_TWOSIDED,two);j["two_sided"]=bool(two);aiString alpha;if(m->Get(AI_MATKEY_GLTF_ALPHAMODE,alpha)==AI_SUCCESS){std::string s=alpha.C_Str();j["alpha_explicit"]=true;j["alpha_mode"]=(s=="MASK"?"mask":s=="BLEND"?"blend":"opaque");}else if(c.a*opacity<.999f)j["alpha_mode"]="blend";float cutoff=.5f;m->Get(AI_MATKEY_GLTF_ALPHACUTOFF,cutoff);j["alpha_cutoff"]=std::clamp(cutoff,0.f,1.f);
        if(ext==".glb"||ext==".gltf"){
            // Physically based values; the renderer derives Source Phong from them.
            float roughness=1,metallic=0,factor=1,glossiness=0;aiColor3D reflectance(1,1,1);
            m->Get(AI_MATKEY_ROUGHNESS_FACTOR,roughness);m->Get(AI_MATKEY_METALLIC_FACTOR,metallic);
            bool specularGlossiness=m->Get(AI_MATKEY_GLOSSINESS_FACTOR,glossiness)==AI_SUCCESS;
            if(m->Get(AI_MATKEY_SPECULAR_FACTOR,factor)!=AI_SUCCESS)factor=1;
            m->Get(AI_MATKEY_COLOR_SPECULAR,reflectance);if(specularGlossiness)factor=1;
            auto unit=[](float x){return std::isfinite(x)?std::clamp(x,0.f,1.f):0.f;};
            j["pbr"]={{"workflow",specularGlossiness?"specular_glossiness":"metallic_roughness"},{"roughness",unit(specularGlossiness?1-glossiness:roughness)},{"metallic",specularGlossiness?0.f:unit(metallic)},
                {"specular",{unit(reflectance.r*factor),unit(reflectance.g*factor),unit(reflectance.b*factor)}}};
            j["source_shading"]="pbr";
        }
        for(int kind=0;kind<3;kind++){aiString ref;aiColor3D emission(0,0,0);
            if(kind==2){if(!j["base_texture"].get<std::string>().empty())continue;m->Get(AI_MATKEY_COLOR_EMISSIVE,emission);if(std::max({emission.r,emission.g,emission.b})<=0)continue;}
            auto type=kind==2?aiTextureType_EMISSIVE:kind==1?aiTextureType_NORMALS:aiTextureType_BASE_COLOR;
            if(m->GetTexture(type,0,&ref)!=AI_SUCCESS){if(kind||m->GetTexture(aiTextureType_DIFFUSE,0,&ref)!=AI_SUCCESS)continue;}
            try{Texture tex;auto embedded=scene->GetEmbeddedTexture(ref.C_Str());fs::path texturePath;auto key=std::string(ref.C_Str());if(!embedded){auto resolved=resources.resolve(key);texturePath=resolved.path;repairedTextures+=resolved.repaired;key=utf8(texturePath.wstring());}auto found=convertedTextures.find(key);if(found!=convertedTextures.end()){const auto& cached=a.textures[found->second];j[kind==1?"normal_texture":"base_texture"]=cached.hash;if(kind==2){j["color"]={std::clamp(emission.r,0.f,1.f),std::clamp(emission.g,0.f,1.f),std::clamp(emission.b,0.f,1.f),j["color"][3].get<float>()};j["unlit"]=true;j["emissive_baked"]=true;}continue;}if(embedded)tex=rawTexture(embedded,options.limits.textureDimension);else tex=makeTexture(readFile(texturePath,MaxTextureFileBytes),options.limits.textureDimension);j[kind==1?"normal_texture":"base_texture"]=tex.hash;auto hash=tex.hash;addTexture(a,std::move(tex),embedded?key:utf8(texturePath.filename().wstring()));for(size_t index=0;index<a.textures.size();++index)if(a.textures[index].hash==hash){convertedTextures[key]=index;break;}}
            catch(const std::exception& e){auto message=std::string("Texture for material ")+name.C_Str()+": "+e.what();a.manifest["warnings"].push_back(message);j["missing_texture"]=true;j["texture_errors"][kind==1?"normal_texture":"base_texture"]=message;}
            if(kind==2&&!j["base_texture"].get<std::string>().empty()){
                j["color"]={std::clamp(emission.r,0.f,1.f),std::clamp(emission.g,0.f,1.f),std::clamp(emission.b,0.f,1.f),j["color"][3].get<float>()};j["unlit"]=true;j["emissive_baked"]=true;
            }
        }a.manifest["materials"].push_back(j);
    }
    progress("Baking scene transforms",.42f);
    float unit=ext==".glb"||ext==".gltf"?39.3700787f:ext==".fbx"?0.393700787f:1.f;
    aiMatrix4x4 rootBasis;
    // FBX metadata can store UnitScaleFactor as float or double. Resolve both explicitly.
    // Assimp axis conversion is disabled above so units and orientation are applied once.
    if(ext==".fbx" && scene->mMetaData) {
        auto numeric=[&](const char* key,double fallback){ double d;float f;int32_t i;
            if(scene->mMetaData->Get(key,d))return d;if(scene->mMetaData->Get(key,f))return double(f);
            if(scene->mMetaData->Get(key,i))return double(i);return fallback;};
        int up=int(numeric("UpAxis",1)),front=int(numeric("FrontAxis",2)),right=int(numeric("CoordAxis",0));
        if(up<0||up>2||front<0||front>2||right<0||right>2||up==front||up==right||front==right)
            throw std::runtime_error("Invalid FBX axis metadata");
        aiVector3D u,f,r;u[up]=float(numeric("UpAxisSign",1));f[front]=float(numeric("FrontAxisSign",1));r[right]=float(numeric("CoordAxisSign",1));
        rootBasis=aiMatrix4x4(r.x,r.y,r.z,0,u.x,u.y,u.z,0,f.x,f.y,f.z,0,0,0,0,1);
        unit*=float(numeric("UnitScaleFactor",1));
        if(!std::isfinite(unit)||unit<=0)throw std::runtime_error("Invalid FBX unit scale");
    }
    a.manifest["unit_scale"]=unit;
    a.manifest["resolved_texture_references"]=repairedTextures;
    std::function<void(const aiNode*,aiMatrix4x4,unsigned)> visit;
    visit=[&](const aiNode* node,aiMatrix4x4 parent,unsigned depth){if(depth>256)throw std::runtime_error("Scene hierarchy is too deep");auto transform=parent*node->mTransformation;aiMatrix3x3 basis(transform),normalMatrix(basis);float det=basis.Determinant();if(std::abs(det)<1e-12f)throw std::runtime_error("Scene contains a singular transform");normalMatrix.Inverse().Transpose();
        for(unsigned n=0;n<node->mNumMeshes;n++){auto m=scene->mMeshes[node->mMeshes[n]];if(!(m->mPrimitiveTypes&aiPrimitiveType_TRIANGLE))continue;checkGeometryStorage(uint64_t(a.vertices.size())+m->mNumVertices,uint64_t(a.indices.size())+uint64_t(m->mNumFaces)*3,options.limits);
            uint32_t base=uint32_t(a.vertices.size()),first=uint32_t(a.indices.size());for(unsigned v=0;v<m->mNumVertices;v++){auto p=transform*m->mVertices[v],normal=normalMatrix*m->mNormals[v];Vertex vert;vert.pos={p.x*unit,p.y*unit,p.z*unit};vert.normal=Vec{normal.x,normal.y,normal.z}.normalized();if(m->HasTextureCoords(0)){vert.u=m->mTextureCoords[0][v].x;vert.v=1-m->mTextureCoords[0][v].y;}if(m->HasTangentsAndBitangents()){auto t=basis*m->mTangents[v];auto b=basis*m->mBitangents[v];auto tv=Vec{t.x,t.y,t.z};tv=(tv-vert.normal*tv.dot(vert.normal)).normalized();vert.tangent={tv.x,tv.y,tv.z,vert.normal.cross(tv).dot({b.x,b.y,b.z})<0?1.f:-1.f};}a.vertices.push_back(vert);}
            for(unsigned f=0;f<m->mNumFaces;f++){auto& face=m->mFaces[f];if(face.mNumIndices!=3)continue;a.indices.push_back(base+face.mIndices[0]);a.indices.push_back(base+face.mIndices[det<0?2:1]);a.indices.push_back(base+face.mIndices[det<0?1:2]);}
            if(a.indices.size()>first)a.manifest["parts"].push_back({{"material",m->mMaterialIndex},{"first",first},{"count",a.indices.size()-first},{"mesh",std::string(m->mName.C_Str())}});
        }for(unsigned i=0;i<node->mNumChildren;i++)visit(node->mChildren[i],transform,depth+1);
    };visit(scene->mRootNode,rootBasis,0);
    if(ext==".obj"&&options.autoUnits&&!a.vertices.empty()){
        // Most OBJ exporters (Blender, Sketchfab) write metres. A model under
        // 8 units across would be a speck in Source inches, so convert it.
        Vec mn=a.vertices[0].pos,mx=mn;for(auto& v:a.vertices){mn={std::min(mn.x,v.pos.x),std::min(mn.y,v.pos.y),std::min(mn.z,v.pos.z)};mx={std::max(mx.x,v.pos.x),std::max(mx.y,v.pos.y),std::max(mx.z,v.pos.z)};}
        auto e=mx-mn;float extent=std::max({e.x,e.y,e.z});
        if(extent>0&&extent<8){for(auto& v:a.vertices)v.pos=v.pos*39.3700787f;a.manifest["unit_scale"]=39.3700787f;
            a.manifest["warnings"].push_back("OBJ files have no units. This model is under 8 units across, so it was treated as metres (x39.37). Use Import scale to change its size.");}
    }
   if(ext==".glb"||ext==".gltf")a.manifest["warnings"].push_back("Source shading approximates glTF materials; metallic/roughness and advanced PBR extensions are not reproduced.");finishImport(a,source,options,progress);return a;
}
void finishImport(Asset& a,const fs::path& source,const Options& o,const Progress& progress){
    progress("Preparing geometry",.45f);
    if(a.vertices.empty()||a.indices.empty())throw std::runtime_error("The file contains no triangle geometry");
    checkGeometryStorage(a.vertices.size(),a.indices.size(),o.limits);
    warnLargeGeometry(a.manifest,a.vertices.size(),a.indices.size(),o.limits);
    auto format=a.manifest.value("format",std::string{});bool pmx=format=="pmx";
    // Blender is Z-up like Source; every other format arrives Y-up.
    bool yUp=o.axis=="y_up"||(o.axis=="auto"&&format!="blend");
    auto rotate=[&](Vec v){
        if(yUp)v=pmx?Vec{v.x,v.z,v.y}:Vec{v.x,-v.z,v.y};
        constexpr float rad=3.14159265358979323846f/180;
        float p=o.rotation[0]*rad,y=o.rotation[1]*rad,r=o.rotation[2]*rad;
        Vec roll{v.x,v.y*std::cos(r)-v.z*std::sin(r),v.y*std::sin(r)+v.z*std::cos(r)};
        Vec pitch{roll.x*std::cos(p)+roll.z*std::sin(p),roll.y,-roll.x*std::sin(p)+roll.z*std::cos(p)};
        return Vec{pitch.x*std::cos(y)-pitch.y*std::sin(y),pitch.x*std::sin(y)+pitch.y*std::cos(y),pitch.z};
    };
    for(auto& v:a.vertices){if(!std::isfinite(v.pos.x)||!std::isfinite(v.pos.y)||!std::isfinite(v.pos.z))throw std::runtime_error("Non-finite vertex data");v.pos=rotate(v.pos)*o.scale;v.normal=rotate(v.normal).normalized();auto t=rotate({v.tangent[0],v.tangent[1],v.tangent[2]});v.tangent={t.x,t.y,t.z,v.tangent[3]*(pmx&&yUp?-1.f:1.f)};}
    if(pmx&&yUp)for(size_t i=0;i<a.indices.size();i+=3)std::swap(a.indices[i+1],a.indices[i+2]);
    applyMaterialOverrides(a,source,o);
    analyzeMaterials(a,progress);
    // Chunk only on triangle boundaries. Keep native indices; expansion happens in bounded Lua batches.
    Json parts=Json::array();for(auto p:a.manifest["parts"]){uint32_t first=p["first"],left=p["count"];while(left){uint32_t count=std::min(left,60000u);p["first"]=first;p["count"]=count;parts.push_back(p);first+=count;left-=count;}}a.manifest["parts"]=parts;
    a.manifest["options"]={{"rotation",o.rotation},{"scale",o.scale},{"axis",o.axis},{"collision",o.collision}};a.manifest["triangles"]=a.indices.size()/3;a.manifest["vertices"]=a.vertices.size();updateBounds(a);
    progress("Preparing collision",.55f);makeCollision(a,o.collision,progress);
    // saveAsset is the single final validation boundary before hashing/writing.
    // Decoding/network acceptance still independently validates all asset data.
}
}
