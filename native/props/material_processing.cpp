#include "material_processing.hpp"
#include <algorithm>
#include <memory>
#include <limits>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>

namespace props {
namespace {
struct UV { double x,y; };
struct Coverage {
    int width,height;
    std::vector<uint8_t> cells;
    uint64_t work=0;
    bool fallback=false;
    Coverage(int w,int h):width(std::min(w,512)),height(std::min(h,512)),cells(size_t(width)*height){}
    void all(){std::fill(cells.begin(),cells.end(),1);fallback=true;}
    // Triangle/rectangle SAT conservatively includes boundary texels. A bounded grid
    // makes cost independent of texture resolution and preserves tiny UV islands.
    bool overlaps(const std::array<UV,3>& p,int x,int y)const {
        UV center{(x+.5)/width,(y+.5)/height};
        for(int i=0;i<3;++i){auto a=p[i],b=p[(i+1)%3];UV axis{a.y-b.y,b.x-a.x};
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(auto v:p){double d=(v.x-center.x)*axis.x+(v.y-center.y)*axis.y;lo=std::min(lo,d);hi=std::max(hi,d);}
            double radius=std::abs(axis.x)*.5/width+std::abs(axis.y)*.5/height;
            if(lo>radius+1e-12||hi< -radius-1e-12)return false;
        }return true;
    }
    void triangle(const Vertex& a,const Vertex& b,const Vertex& c){
        if(fallback)return;
        std::array<UV,3> p{{{a.u,a.v},{b.u,b.v},{c.u,c.v}}};
        for(auto uv:p)if(!std::isfinite(uv.x)||!std::isfinite(uv.y))throw std::runtime_error("Non-finite material UV coordinates");
        double minU=std::min({p[0].x,p[1].x,p[2].x}),maxU=std::max({p[0].x,p[1].x,p[2].x});
        double minV=std::min({p[0].y,p[1].y,p[2].y}),maxV=std::max({p[0].y,p[1].y,p[2].y});
        if(!std::isfinite(minU)||!std::isfinite(maxU)||!std::isfinite(minV)||!std::isfinite(maxV))throw std::runtime_error("Non-finite material UV coordinates");
        if(std::max({std::abs(minU),std::abs(maxU),std::abs(minV),std::abs(maxV)})>1e7||maxU-minU>8||maxV-minV>8){all();return;}
        int u0=int(std::floor(minU)),u1=int(std::floor(maxU)),v0=int(std::floor(minV)),v1=int(std::floor(maxV));
        for(int v=v0;v<=v1;++v)for(int u=u0;u<=u1;++u){
            auto q=p;for(auto& t:q){t.x-=u;t.y-=v;}
            int x0=std::clamp(int(std::floor((minU-u)*width)),0,width-1),x1=std::clamp(int(std::floor((maxU-u)*width)),0,width-1);
            int y0=std::clamp(int(std::floor((minV-v)*height)),0,height-1),y1=std::clamp(int(std::floor((maxV-v)*height)),0,height-1);
            work+=uint64_t(x1-x0+1)*(y1-y0+1);
            if(work>16000000){all();return;}
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)if(!cells[size_t(y)*width+x]&&overlaps(q,x,y))cells[size_t(y)*width+x]=1;
        }
    }
    bool used(int x,int y,int imageW,int imageH)const{return cells[size_t(y*height/imageH)*width+x*width/imageW]!=0;}
};
struct AlphaImage {
    int width=0,height=0;
    std::vector<uint8_t> alpha;
    // Opaque texels need not be visited again for each material using this atlas.
    std::vector<uint32_t> nonOpaque;
    explicit AlphaImage(const Texture& texture){
        int channels=0;
        std::unique_ptr<stbi_uc,void(*)(void*)> rgba(stbi_load_from_memory(texture.png.data(),int(texture.png.size()),&width,&height,&channels,4),stbi_image_free);
        if(!rgba||width<=0||height<=0||width>4096||height>4096)throw std::runtime_error("Cannot analyze normalized texture alpha");
        alpha.resize(size_t(width)*height);for(size_t i=0;i<alpha.size();++i){alpha[i]=rgba.get()[i*4+3];if(alpha[i]<247)nonOpaque.push_back(uint32_t(i));}
    }
    bool cutoutEdge(int x,int y)const {
        bool transparent=false,opaque=false;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){int sx=(x+dx+width)%width,sy=(y+dy+height)%height;auto a=alpha[size_t(sy)*width+sx];transparent|=a<=8;opaque|=a>=247;}
        return transparent&&opaque;
    }
};
}
void analyzeMaterials(Asset& asset,const Progress& progress){
    if(asset.manifest.value("material_processing_version",0)>=2)return;
    auto& materials=asset.manifest.at("materials");auto& parts=asset.manifest.at("parts");
    const auto format=asset.manifest.value("format",std::string{});
    std::vector<bool> inferred(materials.size());
    for(size_t i=0;i<materials.size();++i){auto& m=materials[i];
        if(!m.contains("source_order"))m["source_order"]=i;
        bool explicitMode=m.value("alpha_explicit",format=="gltf"||format=="glb");
        float opacity=m.at("color").at(3).get<float>();
        inferred[i]=!explicitMode&&opacity>=.999f;
        if(!explicitMode&&opacity<.999f){m["alpha_mode"]="blend";m["alpha_detection"]={{"method","material_opacity"}};}
        else if(explicitMode)m["alpha_detection"]={{"method","format_alpha_mode"}};
        else {m["alpha_mode"]="opaque";m["alpha_detection"]={{"method","no_texture_alpha"}};}
    }
    for(size_t t=0;t<asset.textures.size();++t){const auto& texture=asset.textures[t];std::vector<size_t> targets;
        for(size_t i=0;i<materials.size();++i)if(inferred[i]&&materials[i].value("base_texture",std::string{})==texture.hash)targets.push_back(i);
        if(targets.empty())continue;
        // Only a freshly decoded source image can set this nonserialized hint.
        // Network/cache data cannot assert it and still undergo full PNG checks.
        if(texture.alphaKnown&&!texture.hasAlpha){for(auto i:targets)materials[i]["alpha_detection"]={{"method","opaque_texture"},{"minimum",255},{"maximum",255}};continue;}
        progress("Analyzing material coverage",.46f+.05f*float(t)/std::max(size_t(1),asset.textures.size()),"Texture "+std::to_string(t+1)+" of "+std::to_string(asset.textures.size()),t,asset.textures.size());
        AlphaImage image(texture);
        if(image.nonOpaque.empty()){for(auto i:targets)materials[i]["alpha_detection"]={{"method","opaque_texture"},{"minimum",255},{"maximum",255}};continue;}
        for(auto material:targets){auto& m=materials[material];Coverage coverage(image.width,image.height);
            for(const auto& part:parts)if(part.at("material").get<size_t>()==material){size_t first=part.at("first"),count=part.at("count");if(first>asset.indices.size()||count>asset.indices.size()-first||count%3)throw std::runtime_error("Invalid material face range");
                for(size_t j=first;j<first+count;j+=3){auto a=asset.indices[j],b=asset.indices[j+1],c=asset.indices[j+2];if(std::max({a,b,c})>=asset.vertices.size())throw std::runtime_error("Invalid material vertex index");coverage.triangle(asset.vertices[a],asset.vertices[b],asset.vertices[c]);}}
            uint64_t used=0,nonOpaque=0,transparent=0,fractional=0,interior=0;int minimum=255,maximum=0;
            // Exact pixel counts represented by each conservative grid cell.
            for(int y=0;y<coverage.height;++y)for(int x=0;x<coverage.width;++x)if(coverage.cells[size_t(y)*coverage.width+x]){
                int x0=(x*image.width+coverage.width-1)/coverage.width,x1=((x+1)*image.width+coverage.width-1)/coverage.width;
                int y0=(y*image.height+coverage.height-1)/coverage.height,y1=((y+1)*image.height+coverage.height-1)/coverage.height;used+=uint64_t(x1-x0)*(y1-y0);}
            for(auto index:image.nonOpaque){int x=index%image.width,y=index/image.width;if(!coverage.used(x,y,image.width,image.height))continue;int alpha=image.alpha[index];++nonOpaque;minimum=std::min(minimum,alpha);maximum=std::max(maximum,alpha);
                if(alpha<=8)++transparent;else{++fractional;if(!image.cutoutEdge(x,y))++interior;}}
            if(used>nonOpaque)maximum=255;
            std::string mode="opaque";
            if(nonOpaque){mode="mask";
                // Preserve genuine translucent regions. Fractional pixels adjacent to
                // both clear and opaque texels are antialiased cutout edges, not glass.
                if(fractional&&(interior>std::max<uint64_t>(1,used/10000)||!transparent||maximum<247))mode="blend";
            }
            m["alpha_mode"]=mode;m["alpha_detection"]={{"method",coverage.fallback?"bounded_texture_fallback":"uv_coverage"},{"used_texels",used},{"minimum",minimum},{"maximum",maximum},{"transparent_texels",transparent},{"fractional_texels",fractional},{"interior_fractional_texels",interior}};
            if(coverage.fallback&&asset.manifest.contains("warnings"))asset.manifest["warnings"].push_back("Material "+m.value("name",std::string("Material"))+": unusually repeated UVs required whole-texture alpha analysis.");
        }
    }
    asset.manifest["material_processing_version"]=2;
}
}
