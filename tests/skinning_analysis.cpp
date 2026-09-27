// Feasibility numbers for GPU skinning through Source's hardware-skinning path
// (VertexLitGeneric & co.: at most 3 weights per vertex, 53 bone matrices per draw).
// Usage: mmdhl_skinning_analysis <cache root> <asset id> [bone limit=53] [weight limit=3]
#include "runtime.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
using namespace mmd;
int wmain(int argc,wchar_t** argv){try{
 if(argc<3)throw std::runtime_error("Usage: mmdhl_skinning_analysis <cache root> <asset id> [bone limit] [weight limit]");
 auto model=loadAsset(argv[1],utf8(argv[2]));size_t limit=argc>3?std::stoul(argv[3]):53,weights=argc>4?std::stoul(argv[4]):3;
 const auto& m=*model;std::map<int,size_t> types;size_t overWeights=0;float maxDropped=0,sumDropped=0;
 for(auto& v:m.vertices){types[v.type]++;
  std::array<float,4> w=v.weights;size_t nonzero=0;for(float x:w)nonzero+=x>0;
  if(nonzero>weights){overWeights++;std::sort(w.begin(),w.end());float dropped=0;for(size_t k=0;k<nonzero-weights;k++)dropped+=w[4-nonzero+k];maxDropped=std::max(maxDropped,dropped);sumDropped+=dropped;}}
 std::cout<<"model "<<m.name<<" vertices="<<m.vertices.size()<<" triangles="<<m.indices.size()/3<<" bones="<<m.bones.size()<<" materials="<<m.materials.size()<<"\n";
 const char* names[]={"BDEF1","BDEF2","BDEF4","SDEF","QDEF"};
 for(auto& [t,n]:types)std::cout<<"  "<<(t>=0&&t<5?names[t]:"?")<<": "<<n<<" ("<<100.*n/m.vertices.size()<<" %)\n";
 std::cout<<"  vertices with more than "<<weights<<" weights: "<<overWeights<<" ("<<100.*overWeights/m.vertices.size()<<" %), weight dropped mean "<<(overWeights?sumDropped/overWeights:0)<<" max "<<maxDropped<<"\n";
 // Greedy batching in authored triangle order, per material.
 size_t batches=0,maxBones=0,singleBatch=0;std::vector<size_t> perMaterial;
 for(auto& mat:m.materials){std::set<int> current,all;size_t count=mat.count?1:0;
  for(unsigned i=mat.first;i<mat.first+mat.count;i+=3){std::set<int> tri;for(int k=0;k<3;k++){auto& v=m.vertices[m.indices[i+k]];for(int w=0;w<4;w++)if(v.bones[w]>=0&&v.weights[w]>0)tri.insert(v.bones[w]);}
   all.insert(tri.begin(),tri.end());std::set<int> merged=current;merged.insert(tri.begin(),tri.end());
   if(merged.size()>limit){count++;current=tri;}else current=std::move(merged);}
  batches+=count;perMaterial.push_back(count);maxBones=std::max(maxBones,all.size());singleBatch+=count==1;}
 std::sort(perMaterial.rbegin(),perMaterial.rend());
 std::cout<<"  bone batches (<= "<<limit<<" bones): "<<batches<<" draws for "<<m.materials.size()<<" materials; "<<singleBatch<<" materials fit one batch; largest material uses "<<maxBones<<" bones; most batches in one material:";
 for(size_t i=0;i<std::min<size_t>(6,perMaterial.size());i++)std::cout<<" "<<perMaterial[i];std::cout<<"\n";
 // Vertices any vertex morph can move (the CPU-morph share under GPU skinning).
 std::set<int> morphed,uvMorphed;size_t vertexMorphs=0;
 for(auto morph:m.morphs){nanoem_rsize_t n=0;
  if(nanoemModelMorphGetType(morph)==NANOEM_MODEL_MORPH_TYPE_VERTEX){vertexMorphs++;auto entries=nanoemModelMorphGetAllVertexMorphObjects(morph,&n);for(size_t k=0;k<n;k++)morphed.insert(vertexIndex(nanoemModelMorphVertexGetVertexObject(entries[k])));}
  auto type=nanoemModelMorphGetType(morph);if(type==NANOEM_MODEL_MORPH_TYPE_TEXTURE||(type>=NANOEM_MODEL_MORPH_TYPE_UVA1&&type<=NANOEM_MODEL_MORPH_TYPE_UVA4)){auto entries=nanoemModelMorphGetAllUVMorphObjects(morph,&n);for(size_t k=0;k<n;k++)uvMorphed.insert(vertexIndex(nanoemModelMorphUVGetVertexObject(entries[k])));}}
 std::cout<<"  vertex morphs: "<<vertexMorphs<<", vertices they touch: "<<morphed.size()<<" ("<<100.*morphed.size()/m.vertices.size()<<" %); UV-morphed vertices: "<<uvMorphed.size()<<"\n";
 // Position error of 3-weight skinning (smallest weight dropped, rest renormalised)
 // against the authored 4 weights, over deterministic poses rotating every bone
 // by up to `degrees` about a pseudo-random local axis.
 for(float degrees:{10.f,25.f,45.f}){
  std::vector<btTransform> manual(m.bones.size(),btTransform::getIdentity());uint32_t seed=12345;
  auto next=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24);};
  for(auto& t:manual){btVector3 axis(next()*2-1,next()*2-1,next()*2-1);if(axis.length2()<1e-6f)axis={0,1,0};t.setRotation(btQuaternion(axis.normalized(),(next()*2-1)*degrees*SIMD_RADS_PER_DEG));}
  std::vector<btTransform> local,global,skin,effective;evaluatePose(m,manual,std::vector<float>(m.morphs.size(),0.f),nullptr,nullptr,nullptr,local,global,skin,effective);
  double maxError=0,sumError=0;size_t affected=0,over1=0;
  for(auto& v:m.vertices){size_t nonzero=0;for(float x:v.weights)nonzero+=x>0;if(nonzero<=weights)continue;
   auto reference=skinPosition(v,skin);Vertex reduced=v;std::array<int,4> order{0,1,2,3};std::sort(order.begin(),order.end(),[&](int a,int b){return reduced.weights[a]>reduced.weights[b];});
   float kept=0;for(size_t k=0;k<4;k++){if(k<weights)kept+=reduced.weights[order[k]];else{reduced.weights[order[k]]=0;reduced.bones[order[k]]=-1;}}
   if(kept>0)for(auto& w:reduced.weights)w/=kept;
   double error=(skinPosition(reduced,skin)-reference).length();maxError=std::max(maxError,error);sumError+=error;affected++;over1+=error>.01;}
  std::cout<<"  3-weight error at "<<degrees<<" deg poses: mean "<<(affected?sumError/affected:0)<<" PMX (x3.24 = Source units), max "<<maxError<<" PMX, over 0.01 PMX: "<<over1<<" of "<<affected<<" four-weight vertices\n";
 }
 // Classification study: decide which four-weight vertices keep CPU skinning
 // from "training" poses, then measure the error that the kept ones show on
 // independent "validation" poses (different seeds, same angle range).
 auto posesFor=[&](uint32_t seed,const std::vector<float>& angles,int repeats){std::vector<std::vector<btTransform>> out;
  for(int r=0;r<repeats;r++)for(float degrees:angles){std::vector<btTransform> manual(m.bones.size(),btTransform::getIdentity());
   auto next=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24);};
   for(auto& t:manual){btVector3 axis(next()*2-1,next()*2-1,next()*2-1);if(axis.length2()<1e-6f)axis={0,1,0};t.setRotation(btQuaternion(axis.normalized(),(next()*2-1)*degrees*SIMD_RADS_PER_DEG));}
   std::vector<btTransform> local,global,skin,effective;evaluatePose(m,manual,std::vector<float>(m.morphs.size(),0.f),nullptr,nullptr,nullptr,local,global,skin,effective);out.push_back(std::move(skin));}
  return out;};
 auto reduce=[&](const Vertex& v){Vertex r=v;std::array<int,4> order{0,1,2,3};std::sort(order.begin(),order.end(),[&](int a,int b){return r.weights[a]>r.weights[b];});float kept=0;for(int k=0;k<4;k++){if(k<3)kept+=r.weights[order[k]];else{r.weights[order[k]]=0;r.bones[order[k]]=-1;}}if(kept>0)for(auto& w:r.weights)w/=kept;return r;};
 auto maxError=[&](const Vertex& v,const Vertex& r,const std::vector<std::vector<btTransform>>& poses){float e=0;for(auto& p:poses)e=std::max(e,(skinPosition(r,p)-skinPosition(v,p)).length());return e;};
 std::vector<size_t> four;for(size_t i=0;i<m.vertices.size();i++)if(m.vertices[i].type==NANOEM_MODEL_VERTEX_TYPE_BDEF4)four.push_back(i);
 auto validation=posesFor(98765,{20,45,90},4);
 struct Study {const char* label;std::vector<float> angles;int repeats;};
 for(const auto& study:std::vector<Study>{{"2 poses 25/45",{25,45},1},{"12 poses 25/45/90",{25,45,90},4}}){const char* label=study.label;
  auto training=posesFor(12345,study.angles,study.repeats);std::vector<float> trainErr(four.size()),validErr(four.size());
  for(size_t k=0;k<four.size();k++){auto& v=m.vertices[four[k]];auto r=reduce(v);trainErr[k]=maxError(v,r,training);validErr[k]=maxError(v,r,validation);}
  for(float threshold:{.02f,.05f,.1f}){size_t cpu=0;float worstKept=0;for(size_t k=0;k<four.size();k++){if(trainErr[k]>threshold)cpu++;else worstKept=std::max(worstKept,validErr[k]);}
   std::cout<<"  classify on "<<label<<", limit "<<threshold<<" PMX: CPU vertices "<<cpu<<" of "<<four.size()<<" BDEF4; worst kept vertex on validation poses (20/45/90 deg) "<<worstKept<<" PMX\n";}
 }
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
