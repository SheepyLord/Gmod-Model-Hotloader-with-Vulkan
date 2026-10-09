// Hardware skinning: the per-model plan, the bone-limited draw topology and the
// GPU publish path against the CPU reference for the same pose.
// Usage: mmdhl_gpu_skinning_tests <model.pmx> [<cache root> <asset id>...]
#include "runtime.hpp"
#include "secondary.hpp"
#include "gpu_topology.hpp"
#include "jobs.hpp"
#include "rig.hpp"
#include <iostream>
using namespace mmd;
namespace {
btVector3 apply(const float* m,const btVector3& p,bool translate){btVector3 out;for(int r=0;r<3;r++)out[r]=m[r*4]*p.x()+m[r*4+1]*p.y()+m[r*4+2]*p.z()+(translate?m[r*4+3]:0.f);return out;}
// What the vertex shader computes for one rest vertex.
btVector3 gpuPosition(const Snapshot& s,const GpuSkin& plan,const float* rest,size_t i){btVector3 p(0,0,0);for(int k=0;k<3;k++)if(plan.weights[i][k]>0)p+=apply(&s.palette[size_t(plan.bones[i][k])*12],btVector3(rest[0],rest[1],rest[2]),true)*plan.weights[i][k];return p;}
}
int wmain(int argc,wchar_t** argv){try{
 if(argc<2||argc==3)throw std::runtime_error("Usage: mmdhl_gpu_skinning_tests <model.pmx> [<cache root> <asset id>...]");
 int checks=0;auto check=[&](bool ok,const std::string& name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<"\n";};
 std::vector<std::shared_ptr<Model>> models{parse(readFile(argv[1]))};
 for(int i=3;i<argc;i++)models.push_back(loadAsset(argv[2],utf8(argv[i])));
 for(auto& model:models){
  const auto& m=*model;const std::string label=m.name.empty()?"model":m.name;size_t n=m.vertices.size(),triangles=m.indices.size()/3;
  auto plan=m.gpuSkin();auto topology=skinTopology(m,*plan);
  // Topology: every triangle exactly once, material ranges kept, slots valid.
  std::vector<unsigned> seen(triangles,0);bool indices=true,inPart=true,slots=true,bounded=true;
  for(size_t part=0;part<topology.parts.size();part++)for(const auto& batch:topology.parts[part]){const auto& chunk=topology.chunks[batch.chunk];bounded=bounded&&!batch.bones.empty()&&batch.bones.size()<=GpuSkin::MaxBones;
   for(unsigned k=batch.first;k<batch.first+batch.count;k++){unsigned t=chunk.triangles[k/3];if(k%3==0)seen[t]++;auto local=chunk.indices[k];unsigned v=chunk.vertices[local];
    indices=indices&&v==m.indices[t*3+k%3];inPart=inPart&&t*3>=m.materials[part].first&&t*3<m.materials[part].first+m.materials[part].count;
    for(int j=0;j<3;j++){auto s=chunk.slots[local][j];slots=slots&&s<batch.bones.size()&&batch.bones[s]==plan->bones[v][j];}}}
  bool coverage=topology.gpuTriangles+topology.cpuTriangles==triangles;for(size_t t=0;t<triangles;t++)coverage=coverage&&seen[t]==(plan->cpuTriangle[t]?0u:1u);
  check(coverage,label+": every triangle is drawn exactly once, by the hardware or the CPU path");
  check(indices&&inPart,label+": skinned batches reproduce the authored triangles inside their material");
  check(slots&&bounded,label+": batches load at most 53 bones and every vertex slot names its own bone");
  bool weights=true;for(size_t i=0;i<n;i++){if(plan->cpuVertex[i])continue;auto& w=plan->weights[i];weights=weights&&std::abs(w[0]+w[1]+w[2]-1)<1e-5f&&w[0]>=w[1]&&w[1]>=w[2]&&w[2]>=0;}
  check(weights,label+": skinned vertices carry three renormalised weights");
  std::vector<uint8_t> subset(n,0);for(auto i:plan->cpuVertices)subset[i]=1;bool covered=true;
  for(size_t t=0;t<triangles;t++)if(plan->cpuTriangle[t])for(int k=0;k<3;k++)covered=covered&&subset[m.indices[t*3+k]];
  for(auto& group:m.lightOverlaps)for(auto& triangle:group)for(auto v:triangle.vertices)covered=covered&&subset[v];
  check(covered,label+": the CPU-deformed subset covers CPU triangles and overlap-guard candidates");
  size_t cpuVertices=0;for(auto c:plan->cpuVertex)cpuVertices+=c;
  std::cout<<"  "<<label<<": vertices "<<n<<", CPU vertices "<<cpuVertices<<" (4-weight "<<plan->fourWeight<<", moved by dropping "<<plan->droppedWeight<<"), CPU triangles "<<topology.cpuTriangles<<" of "<<triangles
   <<", CPU-deformed subset "<<plan->cpuVertices.size()<<", batches "<<topology.batches<<" for "<<m.materials.size()<<" materials, buffer vertices "<<topology.vertices<<"\n";

  // A rigid Source pose 2000 units from the world origin moves every vertex rigidly, whatever
  // bone carries its weight. MMD control roots above the pelvis (グルーブ) stayed at the world
  // origin and pulled the vertices weighted to them there (issue #6). FK only: no physics.
  {const btTransform placed(btQuaternion(btVector3(0,0,1),.7f),btVector3(1200,-1600,0));double errors[2]={0,0};bool bounded[2]={false,false},published[2]={false,false};
   // Hardware-skinning bounds are bone boxes widened by every vertex morph's reach, active or not.
   float reach=0;for(float r:plan->boneReach)reach=std::max(reach,r);
   for(int hardware=0;hardware<2;hardware++){World host;host.gpuSkinning=hardware;auto& p=host.get(host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}}));p.secondary.reset();
    const auto& rig=*p.sourceRig;std::vector<btTransform> palette;for(auto& bone:rig.bones)palette.push_back(placed*bone.rest);
    p.submitPresentationPose(palette,1,1);p.evaluate(false);p.poseDirty=true;p.publish(1);const auto& s=*p.snapshot;published[hardware]=s.gpu==bool(hardware);
    double radius=0;size_t worst=0,off=0;
    for(size_t i=0;i<n;i++){auto expected=placed*(rigMeshBind(rig)*(toSource(m.vertices[i].position)*rig.scale));radius=std::max(radius,double((expected-placed.getOrigin()).length()));
     auto drawn=s.gpu&&!plan->cpuVertex[i]?gpuPosition(s,*plan,&p.gpuRest->positions[i*3],i):btVector3(s.vertices[i].x,s.vertices[i].y,s.vertices[i].z);
     double e=(drawn-expected).length();off+=e>1;if(e>errors[hardware]){errors[hardware]=e;worst=i;}}
    double slack=2*radius+(s.gpu?4*reach*rig.scale:0.f)+1,extent=std::max((s.minimum-placed.getOrigin()).length(),(s.maximum-placed.getOrigin()).length());bounded[hardware]=extent<=slack;
    std::cout<<"  "<<label<<": rigid pose 2000 units away, "<<(hardware?"hardware":"CPU")<<" skinning: largest vertex error "<<errors[hardware]<<" Source units at vertex "<<worst<<" (";
    const auto& v=m.vertices[worst];const char* separator="";for(int k=0;k<4;k++)if(v.weights[k]!=0&&v.bones[k]>=0){std::cout<<separator<<m.bones[size_t(v.bones[k])].name<<" "<<v.weights[k];separator=", ";}
    std::cout<<"), "<<off<<" vertices off by more than 1 unit, bounds reach "<<extent<<" units from the body (limit "<<slack<<")\n";}
   check(published[0]&&published[1],label+": the far pose publishes through CPU and hardware skinning");
   check(errors[0]<.02&&errors[1]<.02,label+": every vertex follows a rigid Source pose far from the world origin, CPU and hardware skinning");
   check(bounded[0]&&bounded[1],label+": snapshot bounds stay around the far body");}

  // Publish: the same pose through the CPU reference and the hardware path.
  World cpuHost,gpuHost;gpuHost.gpuSkinning=true;
  auto& c=cpuHost.get(cpuHost.create(model,{{"backend","source"},{"secondaryCollision",0}}));auto& g=gpuHost.get(gpuHost.create(model,{{"backend","source"},{"secondaryCollision",0}}));
  c.secondary.reset();g.secondary.reset();
  uint32_t seed=777;auto next=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24);};
  std::vector<btTransform> manual(m.bones.size(),btTransform::getIdentity());
  for(auto& t:manual){btVector3 axis(next()*2-1,next()*2-1,next()*2-1);if(axis.length2()<1e-6f)axis={0,1,0};t.setRotation(btQuaternion(axis.normalized(),(next()*2-1)*20*SIMD_RADS_PER_DEG));}
  for(auto* p:{&c,&g}){std::vector<btTransform> local,global,effective;evaluatePose(m,manual,std::vector<float>(m.morphs.size(),0.f),nullptr,nullptr,nullptr,local,global,p->skin,effective);p->poseDirty=true;p->publish(0);}
  check(g.snapshot->gpu&&!c.snapshot->gpu&&g.snapshot->palette.size()==size_t(plan->palette)*12,label+": the hardware path publishes a palette instead of deforming every vertex");
  double exact=0,dropped=0,subsetError=0;bool inside=true;const float unit=g.scale/Inch;
  auto contains=[&](const btVector3& p){const float e=1e-3f*unit;for(int a=0;a<3;a++)if(p[a]<g.snapshot->minimum[a]-e||p[a]>g.snapshot->maximum[a]+e)return false;return true;};
  for(size_t i=0;i<n;i++){
   const auto& d=c.snapshot->vertices[i];btVector3 reference(d.x,d.y,d.z);
   if(plan->cpuVertex[i]){const auto& s=g.snapshot->vertices[i];subsetError=std::max(subsetError,double((btVector3(s.x,s.y,s.z)-reference).length()));inside=inside&&contains(btVector3(s.x,s.y,s.z));continue;}
   auto p=gpuPosition(*g.snapshot,*plan,&g.gpuRest->positions[i*3],i);inside=inside&&contains(p);
   int influences=0;for(float w:m.vertices[i].weights)influences+=w!=0;
   (influences<=3?exact:dropped)=std::max(influences<=3?exact:dropped,double((p-reference).length()));
   if(subset[i]){const auto& s=g.snapshot->vertices[i];subsetError=std::max(subsetError,double((btVector3(s.x,s.y,s.z)-p).length()));}
  }
  std::cout<<"  "<<label<<": emulated shader vs CPU reference, Source units: up to 3 weights "<<exact<<", fourth weight dropped "<<dropped<<" (limit "<<GpuSkin::MaxDropError*unit<<" at the test poses); CPU subset "<<subsetError<<"\n";
  check(exact<2e-3*unit,label+": hardware skinning reproduces CPU skinning for vertices with up to three weights");
  check(dropped<4*GpuSkin::MaxDropError*unit,label+": dropping a kept fourth weight stays near the plan's limit on an unseen pose");
  check(subsetError<2e-3*unit,label+": the CPU subset matches the reference (SDEF/QDEF) and the shader (shared edges)");
  check(inside,label+": bone-box bounds contain every skinned and CPU-deformed position");

  // Rest data: a vertex morph bumps the rest version once and rewrites only the vertices it touches.
  const nanoem_model_morph_t* morph=nullptr;size_t index=0;
  for(size_t i=0;i<m.morphs.size()&&!morph;i++){nanoem_rsize_t count=0;if(nanoemModelMorphGetType(m.morphs[i])==NANOEM_MODEL_MORPH_TYPE_VERTEX&&nanoemModelMorphGetAllVertexMorphObjects(m.morphs[i],&count)&&count){morph=m.morphs[i];index=i;}}
  if(morph){
   auto before=g.snapshot;auto version=g.snapshot->restVersion;auto rest=g.gpuRest->positions;
   g.poseDirty=true;g.publish(0);check(g.snapshot==before,label+": an unchanged pose and morph state publishes nothing");
   g.morphWeights[index]=1;g.poseDirty=true;g.publish(0);
   nanoem_rsize_t count=0;auto entries=nanoemModelMorphGetAllVertexMorphObjects(morph,&count);std::vector<uint8_t> touched(n,0);bool moved=true;
   for(size_t k=0;k<count;k++){int id=vertexIndex(nanoemModelMorphVertexGetVertexObject(entries[k]));if(id>=0&&size_t(id)<n)touched[size_t(id)]=1;}
   for(size_t i=0;i<n;i++){bool changed=g.gpuRest->changed[i]==g.snapshot->restVersion;if(changed!=bool(touched[i])&&m.vertices[i].type!=NANOEM_MODEL_VERTEX_TYPE_SDEF&&m.vertices[i].type!=NANOEM_MODEL_VERTEX_TYPE_QDEF){moved=false;break;}}
   check(g.snapshot->restVersion==version+1&&moved,label+": a morph weight change stamps exactly the vertices it touches");
   // Weights reach +-2: the bone boxes, which hold each offset once, must still contain the shader's positions.
   // CPU-deformed vertices enter the bounds exactly, so only a vertex outside that subset tests the boxes.
   size_t boxOnly=0;for(size_t i=0;i<n;i++)boxOnly+=touched[i]&&!subset[i];
   if(std::getenv("MMDHL_REQUIRE_GPU_MORPH"))check(boxOnly>0,label+": the vertex morph moves "+std::to_string(boxOnly)+" vertices that only a bone box bounds");
   for(float w:{2.f,-2.f}){g.morphWeights[index]=w;g.poseDirty=true;g.publish(0);bool within=true;
    for(size_t i=0;i<n;i++)if(!plan->cpuVertex[i])within=within&&contains(gpuPosition(*g.snapshot,*plan,&g.gpuRest->positions[i*3],i));
    check(within,label+": bone-box bounds contain vertices displaced by a vertex morph at weight "+std::to_string(int(w)));}
   g.morphWeights[index]=1;g.poseDirty=true;g.publish(0);version=g.snapshot->restVersion-1;
   g.morphWeights[index]=0;g.poseDirty=true;g.publish(0);
   check(g.snapshot->restVersion==version+2&&g.gpuRest->positions==rest,label+": clearing the morph restores the rest positions");
  }
 }
 shutdownJobs();std::cout<<checks<<" hardware-skinning checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";shutdownJobs();return 1;}}
