#include "scene_share.hpp"
#include "rig_writer.hpp"
namespace mmd {
namespace {
Json xyz(const btVector3& v){return {v.x(),v.y(),v.z()};}
btVector3 vec(const Json& j){btVector3 v(j.at(0),j.at(1),j.at(2));for(int i=0;i<3;i++)if(!std::isfinite(v[i]))throw std::runtime_error("Non-finite remote collision pose");return v;}
Bytes encode(const SceneGeometry& g){StudioWriter w;w.alloc(48);w.i(0,0x43534d4d);w.i(4,1);w.i(8,g.kind);w.f(12,g.radius);w.i(16,int(g.vertices.size()));w.i(20,int(g.hullCounts.size()));w.vec(24,g.minimum);w.vec(36,g.maximum);for(auto v:g.vertices)w.vec(w.alloc(12),v);for(int n:g.hullCounts)w.i(w.alloc(4),n);return std::move(w.b);}
template<class T>T at(std::span<const unsigned char> b,size_t offset){if(offset>b.size()||sizeof(T)>b.size()-offset)throw std::runtime_error("Truncated remote collision geometry");T v;std::memcpy(&v,b.data()+offset,sizeof(v));return v;}
}
Json SceneShare::describe(const btVector3& center,float radius,uint64_t consumer){
 // Remote subscribers receive a sphere; the capture keeps what it covers.
 noteSceneInterest(uintptr_t(0x5CE0000000000000ull|consumer),center-btVector3(radius,radius,radius),center+btVector3(radius,radius,radius));
 auto frame=readScene();Json out={{"objects",Json::array()},{"sequence",frame?frame->sequence:0},{"timestamp",frame?frame->timestamp:0}};if(!frame)return out;
 for(auto it=exported.begin();it!=exported.end();)if(it->second.source.expired())it=exported.erase(it);else ++it;
 for(auto& object:frame->objects){auto& g=*object.geometry;auto gc=(g.minimum+g.maximum)*.5f,extent=(g.maximum-g.minimum)*.5f;auto wc=object.transform*gc;float range=radius+extent.length()+object.velocity.length()*.25f;if((wc-center).length2()>range*range)continue;
  auto it=exported.find(&g);if(it==exported.end()){auto bytes=encode(g);auto digest=hash(bytes);it=exported.emplace(&g,Cached{object.geometry,digest,std::move(bytes)}).first;}
  auto q=object.transform.getRotation();out["objects"].push_back({{"id",object.id},{"owner",object.owner},{"bone",object.physicsBone},{"center",xyz(object.localCenter)},{"shape",it->second.key},{"bytes",it->second.bytes.size()},{"static",object.isStatic},{"position",xyz(object.transform.getOrigin())},{"rotation",{q.x(),q.y(),q.z(),q.w()}},{"velocity",xyz(object.velocity)},{"angular",xyz(object.angular)}});
 }return out;
}
Bytes SceneShare::chunk(const std::string& key,size_t offset,size_t count)const{
 for(auto& [_,entry]:exported)if(entry.key==key){if(offset>entry.bytes.size()||count>32768)throw std::runtime_error("Invalid scene chunk request");auto end=std::min(entry.bytes.size(),offset+count);return Bytes(entry.bytes.begin()+offset,entry.bytes.begin()+end);}
 throw std::runtime_error("Collision shape expired");
}
void SceneShare::accept(const std::string& key,std::span<const unsigned char> b){
 if(!validId(key)||hash(b)!=key||at<uint32_t>(b,0)!=0x43534d4d||at<uint32_t>(b,4)!=1)throw std::runtime_error("Invalid collision shape or checksum");
 auto kind=at<int>(b,8),n=at<int>(b,16),h=at<int>(b,20);if(kind<0||kind>2||n<0||h<0||48ull+uint64_t(n)*12+uint64_t(h)*4!=b.size())throw std::runtime_error("Malformed remote collision shape");
 auto g=std::make_shared<SceneGeometry>();g->kind=SceneGeometry::Kind(kind);g->radius=at<float>(b,12);if(!std::isfinite(g->radius)||g->radius<0)throw std::runtime_error("Invalid remote sphere");g->minimum={BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT};g->maximum=-g->minimum;
 for(int i=0;i<n;i++){size_t p=48+size_t(i)*12;btVector3 v(at<float>(b,p),at<float>(b,p+4),at<float>(b,p+8));for(int j=0;j<3;j++)if(!std::isfinite(v[j]))throw std::runtime_error("Invalid remote collision vertex");g->vertices.push_back(v);g->minimum.setMin(v);g->maximum.setMax(v);}
 if(kind==SceneGeometry::Sphere){if(n||h||g->radius<=0)throw std::runtime_error("Malformed remote sphere");g->minimum={-g->radius,-g->radius,-g->radius};g->maximum=-g->minimum;}
 size_t sum=0;for(int i=0;i<h;i++){int count=at<int>(b,48+size_t(n)*12+i*4);if(count<3)throw std::runtime_error("Invalid remote convex");sum+=count;g->hullCounts.push_back(count);}
 if((kind==SceneGeometry::Triangles&&(n%3||h))||(kind==SceneGeometry::Convexes&&sum!=size_t(n)))throw std::runtime_error("Invalid remote collision topology");imported[key]=g;
}
void SceneShare::publish(World& host,const Json& description){
 auto frame=std::make_shared<SceneFrame>();frame->sequence=description.at("sequence");frame->timestamp=description.at("timestamp");if(!std::isfinite(frame->timestamp))throw std::runtime_error("Invalid scene clock");
 for(auto& item:description.at("objects")){auto g=imported.find(item.at("shape").get<std::string>());if(g==imported.end())continue;SceneObject o;o.id=item.at("id");o.owner=item.at("owner");o.physicsBone=item.value("bone",0);o.geometry=g->second;o.isStatic=item.at("static");auto q=item.at("rotation");btQuaternion r(q.at(0),q.at(1),q.at(2),q.at(3));if(!std::isfinite(r.length2())||r.length2()<.5||r.length2()>1.5)throw std::runtime_error("Invalid remote collision rotation");o.transform=btTransform(r.normalized(),vec(item.at("position")));o.velocity=vec(item.at("velocity"));o.angular=vec(item.at("angular"));frame->objects.push_back(std::move(o));}
 host.externalScene.store(frame);
}
}
