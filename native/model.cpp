#include "light_overlaps.hpp"
#include "runtime.hpp"
#include <nanoem_p.h>
#include "secondary.hpp"
#include <ext/mbwc.h>
#include <algorithm>
#include <functional>
#include <numeric>
#include <stdexcept>
#include "jobs.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <intrin.h>
#include <mutex>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace mmd {
btVector3 vec(const float* p) { return p?btVector3(p[0],p[1],p[2]):btVector3(0,0,0); }
// The SIMD deform uses AVX2 and FMA3 instructions. They are separate CPUID
// features (a VM may expose AVX2 without FMA), and both need OS-saved YMM state.
bool simdDeformSupported(bool avx2,bool fma,bool osxsave,uint64_t xcr0){return avx2&&fma&&osxsave&&(xcr0&6)==6;}
btQuaternion quat(const float* p) { return p?btQuaternion(p[0],p[1],p[2],p[3]):btQuaternion::getIdentity(); }
int boneIndex(const nanoem_model_bone_t* p){return p?nanoemModelObjectGetIndex(nanoemModelBoneGetModelObject(p)):-1;}
int bodyIndex(const nanoem_model_rigid_body_t* p){return p?nanoemModelObjectGetIndex(nanoemModelRigidBodyGetModelObject(p)):-1;}
int vertexIndex(const nanoem_model_vertex_t* p){return p?nanoemModelObjectGetIndex(nanoemModelVertexGetModelObject(p)):-1;}
static int morphIndex(const nanoem_model_morph_t* p){return p?nanoemModelObjectGetIndex(nanoemModelMorphGetModelObject(p)):-1;}
btMatrix3x3 basis(){return btMatrix3x3(0,0,1,-1,0,0,0,1,0);}
btVector3 toSource(const btVector3& p){return {p.z(),-p.x(),p.y()};}
btVector3 fromSource(const btVector3& p){return {-p.y(),p.z(),p.x()};}
btTransform convert(const btTransform& t,float s){auto c=basis();return btTransform(c*t.getBasis()*c.transpose(),toSource(t.getOrigin())*s);}
Model::~Model(){if(source)nanoemModelDestroy(source);if(factory)nanoemUnicodeStringFactoryDestroyMBWC(factory);}
std::string Model::text(const nanoem_unicode_string_t* s) const {
    if(!s)return {};
    nanoem_status_t status=NANOEM_STATUS_SUCCESS; nanoem_rsize_t size=0;
    auto b=nanoemUnicodeStringFactoryGetByteArrayEncoding(factory,s,&size,NANOEM_CODEC_TYPE_UTF8,&status);
    std::string result;if(b)result.assign(reinterpret_cast<char*>(b),size);
    nanoemUnicodeStringFactoryDestroyByteArray(factory,b);return result;
}
static void finite(const btVector3& v){for(int j=0;j<3;j++)if(!std::isfinite(v[j])||btFabs(v[j])>1e7f)throw std::runtime_error("Invalid model coordinate");}
static void reference(int i,size_t n){if(i < -1 || (i>=0&&size_t(i)>=n))throw std::runtime_error("Invalid model reference");}
// Every number that reaches Bullet, nanoem or the GPU must be finite and within a
// sane range: one NaN orientation or spring poisons the whole world it joins.
// Only numbers the runtime reads are checked. Exporters write NaN into display
// data (bone tails, local axes) and unused additional UVs, and such models load.
static void validateNumbers(const Model& m){
    constexpr float Length=1e7f,Angle=1e4f,Factor=1e6f,Weight=1e3f,Stiffness=1e12f,Mass=1e15f;
    auto fail=[](const char* what,size_t index){throw std::runtime_error(std::string("Invalid model data: ")+what+" "+std::to_string(index)+" has a non-finite or out-of-range value");};
    auto ok=[](const float* v,int count,float limit){if(!v)return true;for(int k=0;k<count;k++)if(!std::isfinite(v[k])||std::fabs(v[k])>limit)return false;return true;};
    auto one=[&](float v,float limit){return ok(&v,1,limit);};
    auto vec=[&](const btVector3& v,float limit){return one(v.x(),limit)&&one(v.y(),limit)&&one(v.z(),limit);};
    auto count=[](int v,int limit){return v>=0&&v<=limit;};
    for(size_t i=0;i<m.vertices.size();i++){const auto& v=m.vertices[i];
        // UVA1's first two components are the only additional UVs a vertex carries to the GPU.
        if(!ok(v.uv.data(),2,Factor)||!one(v.edge,Factor)||!vec(v.c,Length)||!vec(v.r0,Length)||!vec(v.r1,Length)||!ok(v.extra[0].data(),2,Factor))fail("vertex",i);}
    for(size_t i=0;i<m.bones.size();i++){const auto& b=m.bones[i];auto s=b.source;
        bool good=one(b.coefficient,Weight)&&vec(b.fixedAxis,Length);
        if(auto ik=nanoemModelBoneGetConstraintObject(s)){good=good&&one(nanoemModelConstraintGetAngleLimit(ik),Angle)&&count(nanoemModelConstraintGetNumIterations(ik),65535);
            nanoem_rsize_t n=0;auto links=nanoemModelConstraintGetAllJointObjects(ik,&n);for(size_t k=0;k<n;k++)good=good&&ok(nanoemModelConstraintJointGetLowerLimit(links[k]),3,Angle)&&ok(nanoemModelConstraintJointGetUpperLimit(links[k]),3,Angle);}
        if(!good)fail("bone",i);}
    for(size_t i=0;i<m.materials.size();i++){const auto& v=m.materials[i];
        if(!vec(v.diffuse,Factor)||!vec(v.ambient,Factor)||!vec(v.specular,Factor)||!vec(v.edgeColor,Factor)||!one(v.alpha,Factor)||!one(v.power,Factor)||!one(v.edgeAlpha,Factor)||!one(v.edgeSize,Factor))fail("material",i);}
    for(size_t i=0;i<m.bodies.size();i++){auto s=m.bodies[i];float mass=nanoemModelRigidBodyGetMass(s);
        if(!ok(nanoemModelRigidBodyGetOrientation(s),3,Angle)||!one(mass,Mass)||mass<0||!one(nanoemModelRigidBodyGetLinearDamping(s),Factor)||!one(nanoemModelRigidBodyGetAngularDamping(s),Factor)
           ||!one(nanoemModelRigidBodyGetFriction(s),Factor)||!one(nanoemModelRigidBodyGetRestitution(s),Factor))fail("rigid body",i);}
    for(size_t i=0;i<m.joints.size();i++){auto s=m.joints[i];
        if(!ok(nanoemModelJointGetOrigin(s),3,Length)||!ok(nanoemModelJointGetOrientation(s),3,Angle)||!ok(nanoemModelJointGetLinearLowerLimit(s),3,Length)||!ok(nanoemModelJointGetLinearUpperLimit(s),3,Length)
           ||!ok(nanoemModelJointGetAngularLowerLimit(s),3,Angle)||!ok(nanoemModelJointGetAngularUpperLimit(s),3,Angle)||!ok(nanoemModelJointGetLinearStiffness(s),3,Stiffness)||!ok(nanoemModelJointGetAngularStiffness(s),3,Stiffness))fail("joint",i);}
    for(size_t i=0;i<m.softBodies.size();i++){auto s=m.softBodies[i];float mass=nanoemModelSoftBodyGetTotalMass(s);bool good=one(mass,Mass)&&mass>=0&&one(nanoemModelSoftBodyGetCollisionMargin(s),Length);
        for(float v:{nanoemModelSoftBodyGetVelocityCorrectionFactor(s),nanoemModelSoftBodyGetDampingCoefficient(s),nanoemModelSoftBodyGetDragCoefficient(s),nanoemModelSoftBodyGetLiftCoefficient(s),nanoemModelSoftBodyGetPressureCoefficient(s),
                     nanoemModelSoftBodyGetVolumeConversationCoefficient(s),nanoemModelSoftBodyGetDynamicFrictionCoefficient(s),nanoemModelSoftBodyGetPoseMatchingCoefficient(s),nanoemModelSoftBodyGetRigidContactHardness(s),
                     nanoemModelSoftBodyGetKineticContactHardness(s),nanoemModelSoftBodyGetSoftContactHardness(s),nanoemModelSoftBodyGetAnchorHardness(s),nanoemModelSoftBodyGetSoftVSRigidHardness(s),nanoemModelSoftBodyGetSoftVSKineticHardness(s),
                     nanoemModelSoftBodyGetSoftVSSoftHardness(s),nanoemModelSoftBodyGetSoftVSRigidImpulseSplit(s),nanoemModelSoftBodyGetSoftVSKineticImpulseSplit(s),nanoemModelSoftBodyGetSoftVSSoftImpulseSplit(s),
                     nanoemModelSoftBodyGetLinearStiffnessCoefficient(s),nanoemModelSoftBodyGetAngularStiffnessCoefficient(s),nanoemModelSoftBodyGetVolumeStiffnessCoefficient(s)})good=good&&one(v,Factor);
        for(int v:{nanoemModelSoftBodyGetBendingConstraintsDistance(s),nanoemModelSoftBodyGetClusterCount(s),nanoemModelSoftBodyGetVelocitySolverIterations(s),nanoemModelSoftBodyGetPositionsSolverIterations(s),
                   nanoemModelSoftBodyGetDriftSolverIterations(s),nanoemModelSoftBodyGetClusterSolverIterations(s)})good=good&&count(v,1024);
        if(!good)fail("soft body",i);}
    for(size_t i=0;i<m.morphs.size();i++){auto s=m.morphs[i];nanoem_rsize_t n=0;bool good=true;
        switch(nanoemModelMorphGetType(s)){
        case NANOEM_MODEL_MORPH_TYPE_VERTEX:{auto e=nanoemModelMorphGetAllVertexMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&ok(nanoemModelMorphVertexGetPosition(e[k]),3,Length);break;}
        case NANOEM_MODEL_MORPH_TYPE_TEXTURE:case NANOEM_MODEL_MORPH_TYPE_UVA1: // x and y are applied; UVA2-4 offsets are not
            {auto e=nanoemModelMorphGetAllUVMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&ok(nanoemModelMorphUVGetPosition(e[k]),2,Factor);break;}
        case NANOEM_MODEL_MORPH_TYPE_BONE:{auto e=nanoemModelMorphGetAllBoneMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&ok(nanoemModelMorphBoneGetTranslation(e[k]),3,Length)&&ok(nanoemModelMorphBoneGetOrientation(e[k]),4,Angle);break;}
        case NANOEM_MODEL_MORPH_TYPE_MATERIAL:{auto e=nanoemModelMorphGetAllMaterialMorphObjects(s,&n);for(size_t k=0;k<n;k++){auto x=e[k];
            good=good&&ok(nanoemModelMorphMaterialGetDiffuseColor(x),3,Factor)&&ok(nanoemModelMorphMaterialGetSpecularColor(x),3,Factor)&&ok(nanoemModelMorphMaterialGetAmbientColor(x),3,Factor)&&ok(nanoemModelMorphMaterialGetEdgeColor(x),3,Factor)
                &&one(nanoemModelMorphMaterialGetDiffuseOpacity(x),Factor)&&one(nanoemModelMorphMaterialGetSpecularPower(x),Factor)&&one(nanoemModelMorphMaterialGetEdgeOpacity(x),Factor)&&one(nanoemModelMorphMaterialGetEdgeSize(x),Factor)
                &&ok(nanoemModelMorphMaterialGetDiffuseTextureBlend(x),4,Factor)&&ok(nanoemModelMorphMaterialGetSphereMapTextureBlend(x),4,Factor)&&ok(nanoemModelMorphMaterialGetToonTextureBlend(x),4,Factor);}break;}
        case NANOEM_MODEL_MORPH_TYPE_GROUP:{auto e=nanoemModelMorphGetAllGroupMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&one(nanoemModelMorphGroupGetWeight(e[k]),Weight);break;}
        case NANOEM_MODEL_MORPH_TYPE_FLIP:{auto e=nanoemModelMorphGetAllFlipMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&one(nanoemModelMorphFlipGetWeight(e[k]),Weight);break;}
        case NANOEM_MODEL_MORPH_TYPE_IMPULUSE:{auto e=nanoemModelMorphGetAllImpulseMorphObjects(s,&n);for(size_t k=0;k<n;k++)good=good&&ok(nanoemModelMorphImpulseGetVelocity(e[k]),3,Length)&&ok(nanoemModelMorphImpulseGetTorque(e[k]),3,Length);break;}
        default:break;}
        if(!good)fail("morph",i);}
}
// Exporters leave NaN, infinite or absurd numbers in models that MMD and PMX
// Editor still open: a vertex or UV at NaN, BDEF4 weights of (1, 1, 1, -2) on
// one bone, 1e21 kg anchor bodies, 1e14 joint springs. Repair the loaded nanoem
// data in place before anything reads it, so skinning, morphs, physics and the
// Source rig all see the same values, and note each kind of repair once. Only
// values the checks above would reject change: models that loaded before load
// exactly as before (their cached identity includes the warnings).
static void repairModel(Model& m){
    constexpr float Length=1e7f,Angle=1e4f,Factor=1e6f,Weight=1e3f,Stiffness=1e12f,Mass=1e15f;
    auto model=m.source;
    auto good=[](float v,float limit){return std::isfinite(v)&&std::fabs(v)<=limit;};
    auto valid=[&](const nanoem_f128_t& v,int count,float limit){for(int k=0;k<count;k++)if(!good(v.values[k],limit))return false;return true;};
    // Replace each bad component; true when one was replaced.
    auto fix=[&](nanoem_f128_t& v,int count,float limit,float fallback){bool changed=false;for(int k=0;k<count;k++)if(!good(v.values[k],limit)){v.values[k]=fallback;changed=true;}return changed;};
    auto fixOne=[&](float& v,float limit,float fallback){if(good(v,limit))return false;v=fallback;return true;};
    auto boneOrigin=[&](int bone,nanoem_f128_t& out){for(int k=0;k<3;k++)out.values[k]=0;
        if(bone>=0&&nanoem_rsize_t(bone)<model->num_bones&&valid(model->bones[bone]->origin,3,Length))for(int k=0;k<3;k++)out.values[k]=model->bones[bone]->origin.values[k];};
    // "Repaired <what> (<count>[: names])<detail>." Every repair note starts with
    // "Repaired ", which the addon lists as a note rather than a warning.
    auto note=[&](size_t count,const std::string& what,const std::string& detail,const std::vector<std::string>& names={}){if(!count)return;
        std::string list;for(auto& n:names)list+=(list.empty()?": ":", ")+n;if(!names.empty()&&count>names.size())list+=", ...";
        m.warnings.push_back("Repaired "+what+" ("+std::to_string(count)+list+")"+detail+".");};
    auto listed=[&](std::vector<std::string>& names,const nanoem_unicode_string_t* name){if(names.size()<3)if(auto text=m.text(name);!text.empty())names.push_back(text);};

    std::vector<char> hidden(model->num_vertices,0);size_t lost=0,normals=0,uvs=0,edges=0,sdef=0,weights=0;
    for(nanoem_rsize_t i=0;i<model->num_vertices;i++){auto v=model->vertices[i];
        if(!valid(v->origin,3,Length)){
            // Its triangles are hidden below, which is how MMD shows them; park it
            // on its first bone so bounds and collision fitting stay meaningful.
            boneOrigin(v->num_bone_indices?v->bone_indices[0]:-1,v->origin);hidden[i]=1;lost++;}
        if(fix(v->normal,3,Length,0))normals++; // a zero normal is rebuilt from the faces
        if(fix(v->uv,2,Factor,0)|fix(v->additional_uv[0],2,Factor,0))uvs++;
        if(fixOne(v->edge_size,Factor,1))edges++;
        if(!valid(v->sdef_c,3,Length)||!valid(v->sdef_r0,3,Length)||!valid(v->sdef_r1,3,Length)){
            for(auto p:{&v->sdef_c,&v->sdef_r0,&v->sdef_r1})for(int k=0;k<3;k++)p->values[k]=0;
            if(v->type==NANOEM_MODEL_VERTEX_TYPE_SDEF)v->type=NANOEM_MODEL_VERTEX_TYPE_BDEF2;sdef++;}
        auto n=std::min<nanoem_rsize_t>(v->num_bone_weights,4);auto& w=v->bone_weights.values;bool broken=false;
        for(nanoem_rsize_t k=0;k<n;k++)broken|=!std::isfinite(w[k])||w[k]<0;
        if(broken){
            // Weights split over one bone (1, 1, 1, -2 on the head) are that bone's
            // weight; a stray negative is rounding.
            for(nanoem_rsize_t k=0;k<n;k++)if(!std::isfinite(w[k]))w[k]=0;
            for(nanoem_rsize_t k=1;k<n;k++)for(nanoem_rsize_t j=0;j<k;j++)if(v->bone_indices[j]==v->bone_indices[k]){w[j]+=w[k];w[k]=0;break;}
            float total=0;for(nanoem_rsize_t k=0;k<n;k++){w[k]=std::max(w[k],0.f);total+=w[k];}
            if(total>0)for(nanoem_rsize_t k=0;k<n;k++)w[k]/=total;
            else{nanoem_rsize_t first=0;for(nanoem_rsize_t k=0;k<n;k++)if(v->bone_indices[k]>=0){first=k;break;}for(nanoem_rsize_t k=0;k<n;k++)w[k]=k==first?1.f:0.f;}
            weights++;}
    }
    if(lost){size_t triangles=0;auto index=model->vertex_indices;
        for(nanoem_rsize_t t=0;t+2<model->num_vertex_indices;t+=3){bool touches=false;for(int k=0;k<3;k++)touches|=index[t+k]<model->num_vertices&&hidden[index[t+k]];
            if(touches){index[t+1]=index[t+2]=index[t];triangles++;}}
        note(lost,"vertices without a valid position",": the "+std::to_string(triangles)+" triangles that use them are hidden, as in MMD");}
    note(normals,"vertices with invalid normals",": they are rebuilt from the surrounding faces");
    note(uvs,"vertices with invalid texture coordinates","");
    note(edges,"vertices with an invalid outline size","");
    note(sdef,"vertices with invalid SDEF data",": they blend linearly");
    note(weights,"vertices with negative or invalid skin weights","");

    auto constraint=[&](nanoem_model_constraint_t* c){if(!c)return false;bool changed=fixOne(c->angle_limit,Angle,1);
        if(c->num_iterations<0||c->num_iterations>65535){c->num_iterations=std::clamp(c->num_iterations,0,65535);changed=true;}
        for(nanoem_rsize_t k=0;k<c->num_joints;k++)if(auto j=c->joints[k];j&&(!valid(j->lower_limit,3,Angle)||!valid(j->upper_limit,3,Angle))){
            j->has_angle_limit=0;for(int q=0;q<3;q++)j->lower_limit.values[q]=j->upper_limit.values[q]=0;changed=true;}
        return changed;};
    size_t bones=0;std::vector<std::string> boneNames;
    for(nanoem_rsize_t i=0;i<model->num_bones;i++){auto b=model->bones[i];bool changed=false;
        if(!valid(b->origin,3,Length)){boneOrigin(b->parent_bone_index!=int(i)?b->parent_bone_index:-1,b->origin);changed=true;}
        changed|=fixOne(b->inherent_coefficient,Weight,0);
        if(b->u.flags.has_fixed_axis&&!valid(b->fixed_axis,3,Length)){b->u.flags.has_fixed_axis=0;for(int k=0;k<3;k++)b->fixed_axis.values[k]=0;changed=true;}
        changed|=constraint(b->constraint);
        if(changed){bones++;listed(boneNames,b->name_ja);}
    }
    for(nanoem_rsize_t i=0;i<model->num_constraints;i++)bones+=constraint(model->constraints[i]);
    note(bones,"bones with invalid positions, inherit or IK values","",boneNames);

    size_t materials=0;
    for(nanoem_rsize_t i=0;i<model->num_materials;i++){auto x=model->materials[i];
        bool changed=fix(x->diffuse_color,3,Factor,1)|fix(x->specular_color,3,Factor,0)|fix(x->ambient_color,3,Factor,.5f)|fix(x->edge_color,3,Factor,0)
            |fixOne(x->diffuse_opacity,Factor,1)|fixOne(x->specular_power,Factor,5)|fixOne(x->edge_opacity,Factor,1)|fixOne(x->edge_size,Factor,1);
        materials+=changed;}
    note(materials,"materials with invalid colours or sizes","");

    size_t bodies=0,heavy=0;float heaviest=0;
    for(nanoem_rsize_t i=0;i<model->num_rigid_bodies;i++){auto r=model->rigid_bodies[i];bool changed=false;
        if(!valid(r->origin,3,Length)){boneOrigin(r->is_bone_relative?-1:r->bone_index,r->origin);changed=true;}
        changed|=fix(r->size,3,Length,1)|fix(r->orientation,3,Angle,0);
        if(!std::isfinite(r->mass)){r->mass=1;changed=true;}else if(r->mass<0){r->mass=-r->mass;changed=true;}
        // Modellers pin anchors with absurd masses; 1e15 kg is as immovable.
        if(r->mass>Mass){heaviest=std::max(heaviest,r->mass);r->mass=Mass;heavy++;}
        changed|=fixOne(r->linear_damping,Factor,.5f)|fixOne(r->angular_damping,Factor,.5f)|fixOne(r->friction,Factor,.5f)|fixOne(r->restitution,Factor,0);
        bodies+=changed;}
    note(bodies,"rigid bodies with invalid shapes or physics values","");
    if(heavy){char text[64];std::snprintf(text,sizeof(text),"%.3g",double(heaviest));note(heavy,"rigid body masses above 1e15 kg",std::string(", up to ")+text+" kg: they are 1e15 kg and still act as fixed anchors");}

    size_t joints=0,stiff=0;
    for(nanoem_rsize_t i=0;i<model->num_joints;i++){auto j=model->joints[i];
        bool changed=fix(j->origin,3,Length,0)|fix(j->orientation,3,Angle,0)|fix(j->linear_lower_limit,3,Length,0)|fix(j->linear_upper_limit,3,Length,0)
            |fix(j->angular_lower_limit,3,Angle,0)|fix(j->angular_upper_limit,3,Angle,0),clamped=false;
        for(auto s:{&j->linear_stiffness,&j->angular_stiffness})for(int k=0;k<3;k++){float& v=s->values[k];
            if(!std::isfinite(v)){v=0;changed=true;}else if(std::fabs(v)>Stiffness){v=std::copysign(Stiffness,v);clamped=true;}}
        joints+=changed;stiff+=clamped;}
    note(joints,"joints with invalid positions or limits","");
    note(stiff,"joint springs stiffer than 1e12",": they are 1e12 and stay rigid");

    size_t morphs=0;std::vector<std::string> morphNames;
    for(nanoem_rsize_t i=0;i<model->num_morphs;i++){auto x=model->morphs[i];bool changed=false;auto n=x->num_objects;
        switch(x->type){
        case NANOEM_MODEL_MORPH_TYPE_VERTEX:for(nanoem_rsize_t k=0;k<n;k++)if(!valid(x->u.vertices[k]->position,3,Length)){x->u.vertices[k]->position={};changed=true;}break;
        case NANOEM_MODEL_MORPH_TYPE_TEXTURE:case NANOEM_MODEL_MORPH_TYPE_UVA1:for(nanoem_rsize_t k=0;k<n;k++)if(!valid(x->u.uvs[k]->position,2,Factor)){x->u.uvs[k]->position={};changed=true;}break;
        case NANOEM_MODEL_MORPH_TYPE_BONE:for(nanoem_rsize_t k=0;k<n;k++){auto e=x->u.bones[k];changed|=fix(e->translation,3,Length,0);
            if(!valid(e->orientation,4,Angle)){e->orientation={{0,0,0,1}};changed=true;}}break;
        case NANOEM_MODEL_MORPH_TYPE_MATERIAL:for(nanoem_rsize_t k=0;k<n;k++){auto e=x->u.materials[k];
            float neutral=e->operation==NANOEM_MODEL_MORPH_MATERIAL_OPERATION_TYPE_ADD?0.f:1.f;
            changed|=fix(e->diffuse_color,3,Factor,neutral)|fix(e->specular_color,3,Factor,neutral)|fix(e->ambient_color,3,Factor,neutral)|fix(e->edge_color,3,Factor,neutral)
                |fixOne(e->diffuse_opacity,Factor,neutral)|fixOne(e->specular_power,Factor,neutral)|fixOne(e->edge_opacity,Factor,neutral)|fixOne(e->edge_size,Factor,neutral)
                |fix(e->diffuse_texture_blend,4,Factor,neutral)|fix(e->sphere_map_texture_blend,4,Factor,neutral)|fix(e->toon_texture_blend,4,Factor,neutral);}break;
        case NANOEM_MODEL_MORPH_TYPE_GROUP:for(nanoem_rsize_t k=0;k<n;k++)changed|=fixOne(x->u.groups[k]->weight,Weight,0);break;
        case NANOEM_MODEL_MORPH_TYPE_FLIP:for(nanoem_rsize_t k=0;k<n;k++)changed|=fixOne(x->u.flips[k]->weight,Weight,0);break;
        case NANOEM_MODEL_MORPH_TYPE_IMPULUSE:for(nanoem_rsize_t k=0;k<n;k++)changed|=fix(x->u.impulses[k]->velocity,3,Length,0)|fix(x->u.impulses[k]->torque,3,Length,0);break;
        default:break;}
        if(changed){morphs++;listed(morphNames,x->name_ja);}
    }
    note(morphs,"morphs with invalid offsets","",morphNames);
}
std::shared_ptr<Model> parse(std::span<const unsigned char> bytes){
    if(bytes.size()<8)throw std::runtime_error("Model file size is invalid");
    if(memcmp(bytes.data(),"PMX ",4)&&memcmp(bytes.data(),"Pmd",3))throw std::runtime_error("Select a PMX or PMD model");
    auto m=std::make_shared<Model>();nanoem_status_t status=NANOEM_STATUS_SUCCESS;
    m->factory=nanoemUnicodeStringFactoryCreateMBWC(&status);
    m->source=nanoemModelCreate(m->factory,&status);
    auto b=nanoemBufferCreate(bytes.data(),bytes.size(),&status);
    bool ok=nanoemModelLoadFromBuffer(m->source,b,&status);nanoemBufferDestroy(b);
    if(!ok||status!=NANOEM_STATUS_SUCCESS)throw std::runtime_error("Invalid/truncated PMX/PMD model (nanoem status "+std::to_string(status)+")");
    repairModel(*m);
    auto advise=[&](size_t count,size_t budget,const char* noun){if(count>budget)m->warnings.push_back("Large model: "+std::to_string(count)+" "+noun+". Import continues with full detail; memory use and frame time may be high.");};
    advise(bytes.size(),256ull<<20,"bytes");
    m->name=m->text(nanoemModelGetName(m->source,NANOEM_LANGUAGE_TYPE_JAPANESE));
    nanoem_rsize_t n=0;auto bones=nanoemModelGetAllBoneObjects(m->source,&n);
    advise(n,16384,"bones");
    m->bones.reserve(n);
    for(size_t i=0;i<n;i++){
        auto s=bones[i];Bone v;v.source=s;v.name=m->text(nanoemModelBoneGetName(s,NANOEM_LANGUAGE_TYPE_JAPANESE));
        v.english=m->text(nanoemModelBoneGetName(s,NANOEM_LANGUAGE_TYPE_ENGLISH));v.position=vec(nanoemModelBoneGetOrigin(s));finite(v.position);
        v.parent=boneIndex(nanoemModelBoneGetParentBoneObject(s));reference(v.parent,n);
        v.inherit=boneIndex(nanoemModelBoneGetInherentParentBoneObject(s));reference(v.inherit,n);
        v.coefficient=nanoemModelBoneGetInherentCoefficient(s);v.stage=nanoemModelBoneGetStageIndex(s);
        v.inheritRotation=nanoemModelBoneHasInherentOrientation(s);v.inheritTranslation=nanoemModelBoneHasInherentTranslation(s);
        v.localInherit=nanoemModelBoneHasLocalInherent(s);v.afterPhysics=nanoemModelBoneIsAffectedByPhysicsSimulation(s);
        if(nanoemModelBoneHasFixedAxis(s))v.fixedAxis=vec(nanoemModelBoneGetFixedAxis(s));
        m->bones.push_back(v);
    }
    // Parents and inherit sources come before each bone. Depth-first with an
    // explicit stack: a file may chain every bone, child before parent, deeper
    // than a loading thread's stack. The link that closes a loop (a root bone
    // parented to itself is common) is dropped in the nanoem data too, so the
    // Source rig and physics read the same hierarchy.
    struct Step{int bone,step,from;bool inherit;};
    std::vector<int> visited(n,0);std::vector<Step> pending;
    auto cut=[&](int from,bool inherit,bool self){
        auto& b=m->bones[from];auto s=const_cast<nanoem_model_bone_t*>(b.source);
        if(inherit){b.inherit=-1;b.inheritRotation=b.inheritTranslation=false;s->parent_inherent_bone_index=-1;s->u.flags.has_inherent_orientation=s->u.flags.has_inherent_translation=0;}
        else{b.parent=-1;s->parent_bone_index=-1;}
        m->warnings.push_back("Repaired bone "+std::to_string(from)+(b.name.empty()?"":" ("+b.name+")")+(inherit?": it inherited from a bone that depends on it; that inherit link is removed."
            :self?": it was its own parent and is now a root bone.":": its parent chain looped back to it; it is now a root bone."));};
    auto visit=[&](int root){
        pending.push_back({root,0,-1,false});
        while(!pending.empty()){auto [i,step,from,inherit]=pending.back();pending.pop_back();if(i<0)continue;
            if(step==0){if(visited[i]==2)continue;if(visited[i]==1){cut(from,inherit,from==i);continue;}visited[i]=1;pending.push_back({i,1,-1,false});pending.push_back({m->bones[i].parent,0,i,false});}
            else if(step==1){pending.push_back({i,2,-1,false});if((m->bones[i].inheritRotation||m->bones[i].inheritTranslation)&&m->bones[i].inherit!=i)pending.push_back({m->bones[i].inherit,0,i,true});}
            else{visited[i]=2;m->order.push_back(i);}
        }
    };
    std::vector<unsigned> sorted(n);std::iota(sorted.begin(),sorted.end(),0u);
    std::stable_sort(sorted.begin(),sorted.end(),[&](unsigned a,unsigned c){return m->bones[a].stage<m->bones[c].stage;});for(auto i:sorted)visit(i);
    auto verts=nanoemModelGetAllVertexObjects(m->source,&n);advise(n,2000000,"vertices");
    m->vertices.reserve(n);m->minimum=btVector3(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT);m->maximum=-m->minimum;
    for(size_t i=0;i<n;i++){
        auto s=verts[i];Vertex v;v.position=vec(nanoemModelVertexGetOrigin(s));v.normal=vec(nanoemModelVertexGetNormal(s));finite(v.position);finite(v.normal);
        auto uv=nanoemModelVertexGetTexCoord(s);v.uv={uv[0],uv[1]};v.type=nanoemModelVertexGetType(s);v.edge=nanoemModelVertexGetEdgeSize(s);
        v.c=vec(nanoemModelVertexGetSdefC(s));v.r0=vec(nanoemModelVertexGetSdefR0(s));v.r1=vec(nanoemModelVertexGetSdefR1(s));
        float total=0;for(int j=0;j<4;j++){
            v.bones[j]=boneIndex(nanoemModelVertexGetBoneObject(s,j));reference(v.bones[j],m->bones.size());
            float w=nanoemModelVertexGetBoneWeight(s,j);if(!std::isfinite(w)||w<0)throw std::runtime_error("Invalid skin weight");
            v.weights[j]=v.bones[j]>=0?w:0;total+=v.weights[j];
            auto extra=nanoemModelVertexGetAdditionalUV(s,j);if(extra)std::copy_n(extra,4,v.extra[j].begin());
        }
        if(total>1e-8f)for(float& w:v.weights)w/=total;
        m->minimum.setMin(v.position);m->maximum.setMax(v.position);m->vertices.push_back(v);
    }
    if(!n)m->minimum=m->maximum=btVector3(0,0,0);
    auto indices=nanoemModelGetAllVertexIndices(m->source,&n);advise(n,6000000,"triangle indices");if(n%3)throw std::runtime_error("Invalid triangle count");
    m->indices.assign(indices,indices+n);for(auto i:m->indices)if(i>=m->vertices.size())throw std::runtime_error("Triangle vertex outside model");
    auto materials=nanoemModelGetAllMaterialObjects(m->source,&n);advise(n,512,"materials");
    size_t cursor=0;
    auto texture=[&](const nanoem_model_texture_t* t){return t?m->text(nanoemModelTextureGetPath(t)):std::string();};
    for(size_t i=0;i<n;i++){
        auto s=materials[i];Material v;v.name=m->text(nanoemModelMaterialGetName(s,NANOEM_LANGUAGE_TYPE_JAPANESE));
        v.first=cursor;v.count=nanoemModelMaterialGetNumVertexIndices(s);cursor+=v.count;
        if(v.count%3||cursor>m->indices.size())throw std::runtime_error("Invalid material triangle range");
        v.base=texture(nanoemModelMaterialGetDiffuseTextureObject(s));v.sphere=texture(nanoemModelMaterialGetSphereMapTextureObject(s));v.toon=texture(nanoemModelMaterialGetToonTextureObject(s));
        v.toonIndex=nanoemModelMaterialIsToonShared(s)?nanoemModelMaterialGetToonTextureIndex(s):-1;
        v.diffuse=vec(nanoemModelMaterialGetDiffuseColor(s));v.ambient=vec(nanoemModelMaterialGetAmbientColor(s));v.specular=vec(nanoemModelMaterialGetSpecularColor(s));
        v.edgeColor=vec(nanoemModelMaterialGetEdgeColor(s));v.alpha=nanoemModelMaterialGetDiffuseOpacity(s);v.power=nanoemModelMaterialGetSpecularPower(s);
        v.edgeAlpha=nanoemModelMaterialGetEdgeOpacity(s);v.edgeSize=nanoemModelMaterialGetEdgeSize(s);v.sphereMode=nanoemModelMaterialGetSphereMapTextureType(s);
        v.twoSided=nanoemModelMaterialIsCullingDisabled(s);v.edge=nanoemModelMaterialIsEdgeEnabled(s);v.shadow=nanoemModelMaterialIsShadowMapEnabled(s);m->materials.push_back(v);
    }
    if(cursor!=m->indices.size())throw std::runtime_error("Materials do not cover all triangles");
    // Unit normals: a zero one would make its tangent frame NaN. Use the adjacent
    // faces' normal instead, or up for a vertex without area.
    {std::vector<btVector3> faces;for(size_t i=0;i<m->vertices.size();i++){auto& normal=m->vertices[i].normal;float length=normal.length();if(length>1e-6f){normal/=length;continue;}
        if(faces.empty()){faces.assign(m->vertices.size(),btVector3(0,0,0));for(size_t t=0;t+2<m->indices.size();t+=3){auto a=m->indices[t],b=m->indices[t+1],c=m->indices[t+2];auto f=(m->vertices[b].position-m->vertices[a].position).cross(m->vertices[c].position-m->vertices[a].position);for(auto k:{a,b,c})faces[k]+=f;}}
        normal=faces[i].length2()>1e-20f?faces[i].normalized():btVector3(0,1,0);}}
    m->tangents.resize(m->vertices.size(),btVector3(0,0,0));m->tangentSigns.resize(m->vertices.size(),1);std::vector<btVector3> bitangents(m->vertices.size(),btVector3(0,0,0));
    for(size_t i=0;i<m->indices.size();i+=3){auto ia=m->indices[i],ib=m->indices[i+1],ic=m->indices[i+2];auto& a=m->vertices[ia];auto& b=m->vertices[ib];auto& c=m->vertices[ic];auto e=b.position-a.position,f=c.position-a.position;float u=b.uv[0]-a.uv[0],v=b.uv[1]-a.uv[1],s=c.uv[0]-a.uv[0],t=c.uv[1]-a.uv[1],d=u*t-v*s;if(btFabs(d)<1e-9f)continue;auto x=(e*t-f*v)/d,y=(f*u-e*s)/d;for(auto index:{ia,ib,ic}){m->tangents[index]+=x;bitangents[index]+=y;}}
    for(size_t i=0;i<m->vertices.size();i++){auto n=m->vertices[i].normal;auto& t=m->tangents[i];t-=n*n.dot(t);if(t.length2()<1e-8f)t=n.cross(btFabs(n.y())<.9f?btVector3(0,1,0):btVector3(1,0,0));t.normalize();m->tangentSigns[i]=n.cross(t).dot(bitangents[i])<0?-1.f:1.f;}
    auto bodies=nanoemModelGetAllRigidBodyObjects(m->source,&n);advise(n,8192,"rigid bodies");m->bodies.assign(bodies,bodies+n);
    for(auto s:m->bodies){reference(boneIndex(nanoemModelRigidBodyGetBoneObject(s)),m->bones.size());finite(vec(nanoemModelRigidBodyGetOrigin(s)));finite(vec(nanoemModelRigidBodyGetShapeSize(s)));}
    auto joints=nanoemModelGetAllJointObjects(m->source,&n);advise(n,16384,"joints");m->joints.assign(joints,joints+n);
    for(size_t i=0;i<m->joints.size();i++){
        auto s=m->joints[i];Model::JointReference ref{s->rigid_body_a_index,s->rigid_body_b_index,true,{}};
        auto valid=[&](int index){return index==-1||(index>=0&&size_t(index)<m->bodies.size());};
        if(!valid(ref.a)||!valid(ref.b))ref.reason="body index outside model";
        else if(ref.a==ref.b)ref.reason=ref.a<0?"both endpoints are world anchors":"identical body references";
        if(!ref.reason.empty()){
            ref.valid=false;
            m->warnings.push_back("Skipped joint "+std::to_string(i)+" ("+m->text(nanoemModelJointGetName(s,NANOEM_LANGUAGE_TYPE_JAPANESE))+"): "+ref.reason);
        }
        m->jointReferences.push_back(ref);
    }
    auto soft=nanoemModelGetAllSoftBodyObjects(m->source,&n);advise(n,256,"soft bodies");m->softBodies.assign(soft,soft+n);
    auto morphs=nanoemModelGetAllMorphObjects(m->source,&n);advise(n,16384,"morphs");m->morphs.assign(morphs,morphs+n);
    for(auto s:m->morphs)m->morphNames.push_back(m->text(nanoemModelMorphGetName(s,NANOEM_LANGUAGE_TYPE_JAPANESE)));
    validateNumbers(*m);
    buildLightOverlaps(*m);
    m->id=hash(bytes);return m;
}
Json Model::info() const {
    Json j={{"id",id},{"name",name},{"vertices",vertices.size()},{"triangles",indices.size()/3},{"bones",bones.size()},{"rigidBodies",bodies.size()},{"joints",joints.size()},{"softBodies",softBodies.size()},{"warnings",warnings},{"morphs",morphNames}};
    j["bounds"]={{minimum.x(),minimum.y(),minimum.z()},{maximum.x(),maximum.y(),maximum.z()}};
    j["morphInfo"]=Json::array();for(size_t i=0;i<morphs.size();i++)j["morphInfo"].push_back({{"name",morphNames[i]},{"category",int(nanoemModelMorphGetCategory(morphs[i]))},{"type",int(nanoemModelMorphGetType(morphs[i]))}});
    j["boneList"]=Json::array();for(auto& b:bones)j["boneList"].push_back({{"name",b.name},{"english",b.english},{"parent",b.parent},{"position",{b.position.x(),b.position.y(),b.position.z()}}});
    auto rgb=[](const btVector3& v){return Json{v.x(),v.y(),v.z()};};
    j["materials"]=Json::array();for(auto& v:materials)j["materials"].push_back({{"name",v.name},{"base",v.base},{"sphere",v.sphere},{"toon",v.toon},{"toonIndex",v.toonIndex},{"sphereMode",v.sphereMode},{"alpha",v.alpha},{"alphaTexture",v.alphaTexture},{"translucentTexture",v.translucentTexture},{"diffuse",rgb(v.diffuse)},{"ambient",rgb(v.ambient)},{"specular",rgb(v.specular)},{"power",v.power},{"edgeColor",rgb(v.edgeColor)},{"edgeAlpha",v.edgeAlpha},{"edgeSize",v.edgeSize},{"shadow",v.shadow},{"twoSided",v.twoSided},{"edge",v.edge},{"first",v.first},{"count",v.count}});
    for(size_t i=0;i<materials.size();i++)j["materials"][i]["path"]=materialPath(id,i,materials[i].name);
    return j;
}
static const btTransform& matrix(const Vertex& v,int i,const std::vector<btTransform>& transforms){static const auto identity=btTransform::getIdentity();return v.bones[i]>=0&&size_t(v.bones[i])<transforms.size()?transforms[v.bones[i]]:identity;}
static btTransform dualQuaternion(const Vertex& v,const std::vector<btTransform>& t){
    btQuaternion real(0,0,0,0),dual(0,0,0,0),referenceQ=matrix(v,0,t).getRotation();
    for(int i=0;i<4;i++)if(v.weights[i]>0){const auto& m=matrix(v,i,t);auto r=m.getRotation();float w=v.weights[i]*(r.dot(referenceQ)<0?-1.f:1.f);auto p=m.getOrigin();auto d=(btQuaternion(p.x(),p.y(),p.z(),0)*r)*.5f;real+=r*w;dual+=d*w;}
    float length=real.length();if(length<1e-7f)return btTransform::getIdentity();real/=length;dual/=length;dual-=real*real.dot(dual);
    auto p=(dual*real.inverse())*2;return btTransform(real,btVector3(p.x(),p.y(),p.z()));
}
btVector3 skinPosition(const Vertex& v,const std::vector<btTransform>& t){
    if(v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF)return dualQuaternion(v,t)*v.position;
    if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF){auto& a=matrix(v,0,t);auto& b=matrix(v,1,t);float w=v.weights[0];auto rw=v.r0*w+v.r1*(1-w);auto r0=v.c+(v.r0-rw)*.5f;auto r1=v.c+(v.r1-rw)*.5f;auto q=a.getRotation().slerp(b.getRotation(),1-w);return quatRotate(q,v.position-v.c)+(a*r0)*w+(b*r1)*(1-w);}
    btVector3 p(0,0,0);float sum=0;for(int i=0;i<4;i++){p+=(matrix(v,i,t)*v.position)*v.weights[i];sum+=v.weights[i];}return sum>1e-7f?p:v.position;
}
btVector3 skinNormal(const Vertex& v,const std::vector<btTransform>& t){
    btVector3 p(0,0,0);
    if(v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF)p=dualQuaternion(v,t).getBasis()*v.normal;
    else if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF)p=quatRotate(matrix(v,0,t).getRotation().slerp(matrix(v,1,t).getRotation(),1-v.weights[0]),v.normal);
    else for(int i=0;i<4;i++)p+=(matrix(v,i,t).getBasis()*v.normal)*v.weights[i];
    return p.length2()>1e-12f?p.normalized():btVector3(0,1,0);
}
SkinnedFrame skinFrame(const Vertex& v,const std::vector<btTransform>& t,const btVector3& tangent){
    SkinnedFrame out;
    if(v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF){
        const auto transform=dualQuaternion(v,t);out.position=transform*v.position;
        out.normal=transform.getBasis()*v.normal;out.tangent=transform.getBasis()*tangent;
    }else if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF){
        const auto& a=matrix(v,0,t);const auto& b=matrix(v,1,t);float w=v.weights[0];
        const auto rw=v.r0*w+v.r1*(1-w),r0=v.c+(v.r0-rw)*.5f,r1=v.c+(v.r1-rw)*.5f;
        const auto rotation=a.getRotation().slerp(b.getRotation(),1-w);
        out.position=quatRotate(rotation,v.position-v.c)+(a*r0)*w+(b*r1)*(1-w);
        out.normal=quatRotate(rotation,v.normal);out.tangent=quatRotate(rotation,tangent);
    }else{
        out.position=out.normal=out.tangent=btVector3(0,0,0);float sum=0;
        for(int i=0;i<4;i++)if(v.weights[i]!=0){const auto& transform=matrix(v,i,t);float w=v.weights[i];
            out.position+=(transform*v.position)*w;out.normal+=(transform.getBasis()*v.normal)*w;
            out.tangent+=(transform.getBasis()*tangent)*w;sum+=w;
        }
        if(sum<=1e-7f)out.position=v.position;
    }
    out.normal=out.normal.length2()>1e-12f?out.normal.normalized():btVector3(0,1,0);
    out.tangent=out.tangent.length2()>1e-12f?out.tangent.normalized():btVector3(0,1,0);
    return out;
}
// Group morphs multiply a weight by each link's coefficient (up to 1000), so a
// chain of them reaches infinity after 13 links and hands Bullet an infinite
// impulse. Every derived weight stays within the largest coefficient a model
// may declare. Groups that name the same child several times expand
// exponentially with depth; the visit budget stops a crafted lattice there.
constexpr float MaxExpandedMorph=1e3f;constexpr size_t MorphExpansionBudget=size_t(1)<<20;
static float boundedMorph(float w){return std::isfinite(w)?std::clamp(w,-MaxExpandedMorph,MaxExpandedMorph):0.f;}
const std::vector<float>& Instance::expandedMorphs() const {
    if(cachedMorphInputs==morphWeights&&cachedMorphWeights.size()==morphWeights.size())return cachedMorphWeights;
    const auto& instance=*this;auto& m=*instance.model;auto weights=instance.morphWeights;std::vector<int> path(weights.size());size_t visits=0;
    std::function<void(int,float,int)> expand=[&](int i,float weight,int depth){
        if(i<0||size_t(i)>=weights.size()||depth>64||path[i]||++visits>MorphExpansionBudget)return;
        auto s=m.morphs[i];int type=nanoemModelMorphGetType(s);nanoem_rsize_t n=0;path[i]=1;
        auto add=[&](int target,float w){if(target>=0&&size_t(target)<weights.size()){w=boundedMorph(w);weights[target]=boundedMorph(weights[target]+w);expand(target,w,depth+1);}};
        if(type==NANOEM_MODEL_MORPH_TYPE_GROUP){auto entries=nanoemModelMorphGetAllGroupMorphObjects(s,&n);for(size_t k=0;k<n;k++)add(morphIndex(nanoemModelMorphGroupGetMorphObject(entries[k])),weight*nanoemModelMorphGroupGetWeight(entries[k]));}
        if(type==NANOEM_MODEL_MORPH_TYPE_FLIP){auto entries=nanoemModelMorphGetAllFlipMorphObjects(s,&n);if(n&&weight>0){size_t k=std::min(n-1,size_t(weight*n));add(morphIndex(nanoemModelMorphFlipGetMorphObject(entries[k])),nanoemModelMorphFlipGetWeight(entries[k]));}}
        path[i]=0;
    };
    for(size_t i=0;i<weights.size();i++)if(instance.morphWeights[i])expand(int(i),instance.morphWeights[i],0);
    cachedMorphInputs=morphWeights;cachedMorphWeights=std::move(weights);return cachedMorphWeights;
}
void evaluatePose(const Model& m,const std::vector<btTransform>& manual,const std::vector<float>& weights,const std::vector<int>* sourceControl,const std::vector<btTransform>* sourcePose,const PoseHooks* hooks,std::vector<btTransform>& local,std::vector<btTransform>& global,std::vector<btTransform>& skin,std::vector<btTransform>& effectiveOut){
    local=manual;global.resize(local.size());skin.resize(local.size());
    for(size_t i=0;i<weights.size();i++)if(weights[i]!=0){nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllBoneMorphObjects(m.morphs[i],&n);for(size_t k=0;k<n;k++){
        int id=boneIndex(nanoemModelMorphBoneGetBoneObject(entries[k]));if(id<0)continue;
        local[id].getOrigin()+=vec(nanoemModelMorphBoneGetTranslation(entries[k]))*weights[i];
        auto q=quat(nanoemModelMorphBoneGetOrientation(entries[k]));if(q.length2()>1e-10f)local[id].setRotation(local[id].getRotation()*btQuaternion::getIdentity().slerp(q.normalized(),weights[i]));
    }}
    effectiveOut.assign(local.size(),btTransform::getIdentity());auto& effective=effectiveOut;
    auto controlled=[&](size_t i){return sourceControl&&(*sourceControl)[i]>=0;};
    auto rebuild=[&](){for(auto i:m.order){auto& b=m.bones[i];auto t=local[i];
        if(b.inherit>=0&&b.inherit!=int(i)){auto inherited=b.localInherit?local[b.inherit]:effective[b.inherit];if(b.inheritRotation)t.setRotation(t.getRotation()*btQuaternion::getIdentity().slerp(inherited.getRotation(),b.coefficient));if(b.inheritTranslation)t.getOrigin()+=inherited.getOrigin()*b.coefficient;}
        if(b.fixedAxis.length2()>1e-8f){auto axis=b.fixedAxis.normalized();auto q=t.getRotation();auto projected=axis*axis.dot(btVector3(q.x(),q.y(),q.z()));btQuaternion twist(projected.x(),projected.y(),projected.z(),q.w());t.setRotation(twist.length2()>1e-8f?twist.normalized():btQuaternion::getIdentity());}
        effective[i]=t;
        auto rest=b.position-(b.parent>=0?m.bones[b.parent].position:btVector3(0,0,0));t.getOrigin()+=rest;
        global[i]=b.parent>=0?global[b.parent]*t:t;
        if(controlled(i))global[i]=(*sourcePose)[i];
        else if(hooks&&hooks->physics)hooks->physics(i,global[i],effective[i],rest,b.parent>=0?&global[b.parent]:nullptr);
        if(sourceControl){effective[i]=b.parent>=0?global[b.parent].inverse()*global[i]:global[i];effective[i].getOrigin()-=rest;}
        skin[i]=global[i]*btTransform(btQuaternion::getIdentity(),-b.position);
    }};rebuild();
    nanoem_rsize_t count=0;auto constraints=nanoemModelGetAllConstraintObjects(m.source,&count);
    auto physicsDriven=[&](int i){return hooks&&hooks->driven&&hooks->driven(size_t(i));};
    for(size_t k=0;k<count;k++){
        auto constraint=constraints[k];int goal=boneIndex(nanoemModelConstraintGetTargetBoneObject(constraint)),end=boneIndex(nanoemModelConstraintGetEffectorBoneObject(constraint));if(goal<0||end<0)continue;
        nanoem_rsize_t links=0;auto chain=nanoemModelConstraintGetAllJointObjects(constraint,&links);bool driven=controlled(size_t(end))||physicsDriven(end);
        for(size_t l=0;l<links;l++){int i=boneIndex(nanoemModelConstraintJointGetBoneObject(chain[l]));if(i>=0&&(controlled(size_t(i))||physicsDriven(i)))driven=true;}
        if(driven)continue;auto target=global[goal].getOrigin();int iterations=std::clamp(nanoemModelConstraintGetNumIterations(constraint),0,128);
        for(int iteration=0;iteration<iterations;iteration++){
            if((global[end].getOrigin()-target).length2()<1e-8f)break;
            for(size_t l=0;l<links;l++){int i=boneIndex(nanoemModelConstraintJointGetBoneObject(chain[l]));if(i<0)continue;
                auto inverse=global[i].inverse();auto a=inverse*global[end].getOrigin(),b=inverse*target;if(a.length2()<1e-10f||b.length2()<1e-10f)continue;a.normalize();b.normalize();auto axis=a.cross(b);if(axis.length2()<1e-10f)continue;axis.normalize();
                if(m.bones[i].fixedAxis.length2()>1e-8f)axis=m.bones[i].fixedAxis.normalized()* (axis.dot(m.bones[i].fixedAxis)<0?-1.f:1.f);
                float angle=std::min(btAcos(std::clamp(a.dot(b),-1.f,1.f)),std::max(0.f,nanoemModelConstraintGetAngleLimit(constraint))*float(l+1));auto q=local[i].getRotation()*btQuaternion(axis,angle);
                if(nanoemModelConstraintJointHasAngleLimit(chain[l])){float z,y,x;btMatrix3x3(q).getEulerZYX(z,y,x);auto lo=vec(nanoemModelConstraintJointGetLowerLimit(chain[l])),hi=vec(nanoemModelConstraintJointGetUpperLimit(chain[l]));btMatrix3x3 r;r.setEulerZYX(std::clamp(x,lo.x(),hi.x()),std::clamp(y,lo.y(),hi.y()),std::clamp(z,lo.z(),hi.z()));r.getRotation(q);}
                local[i].setRotation(q.normalized());rebuild();
            }
        }
    }
}
std::shared_ptr<const std::vector<btTransform>> Instance::shareManual(){if(!manualShared||manualSharedVersion!=manualVersion){manualShared=std::make_shared<const std::vector<btTransform>>(manual);manualSharedVersion=manualVersion;}return manualShared;}
void Instance::evaluate(bool physics){
    auto started=std::chrono::steady_clock::now();
    PoseHooks hooks;
    if(physics&&(secondary||!bodies.empty())){
        hooks.physics=[&](size_t i,btTransform& g,btTransform& effective,const btVector3& rest,const btTransform* parentGlobal){
            if(secondary){g=secondary->feedback(i,g,parentGlobal);return;}
            if(drivers[i]<0)return;auto& body=*bodies[drivers[i]];auto worldPose=placement.inverse()*body.rigid->getWorldTransform()*body.offset.inverse();
            auto c=basis();btTransform pose(c.transpose()*worldPose.getBasis()*c,fromSource(worldPose.getOrigin())/scale);
            if(body.mode==2&&!body.core)pose.setOrigin(g.getOrigin());g=pose;
            effective=parentGlobal?parentGlobal->inverse()*pose:pose;effective.getOrigin()-=rest;
        };
        hooks.driven=[&](size_t i){return drivers[i]>=0||(secondary&&secondary->drives(i));};
    }
    // Evaluate in place: feedback reads the live `local` array for mode-2 bodies.
    evaluatePose(*model,manual,expandedMorphs(),sourceRig?&sourceControl:nullptr,sourceRig?&sourcePose:nullptr,physics?&hooks:nullptr,local,global,skin,effectiveScratch);
    evaluateMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
void Instance::ensureSnapshot(){if(!snapshot||poseDirty)publish(owner->time);}
namespace {
constexpr size_t Lanes=8;
bool avx2FmaAvailable(){static const bool value=[]{int info[4];__cpuid(info,1);bool osxsave=(info[2]&(1<<27))!=0;
 return simdDeformSupported(IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)!=0,(info[2]&(1<<12))!=0,osxsave,osxsave?_xgetbv(0):0);}();return value;}
// Vertices with the same skinning type and bone set skin with the same
// matrices, so they are gathered into padded groups that a SIMD block walks
// eight at a time. SDEF/QDEF vertices keep the scalar reference path.
void buildSkinLayout(const Model& m,Model::SkinLayout& out){
 size_t n=m.vertices.size();out.identityBone=unsigned(m.bones.size());out.sorted.assign(n,-1);
 struct Key {int type;std::array<int,4> bones;std::array<float,4> weights;unsigned index;};
 std::vector<Key> keys;keys.reserve(n);
 for(size_t i=0;i<n;i++){const auto& v=m.vertices[i];
  if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF||v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF){out.scalar.push_back(unsigned(i));continue;}
  Key k{0,{-1,-1,-1,-1},{0,0,0,0},unsigned(i)};float sum=0;int count=0;
  for(int j=0;j<4;j++)if(v.weights[j]!=0){int b=v.bones[j];k.bones[count]=(b>=0&&size_t(b)<m.bones.size())?b:int(out.identityBone);k.weights[count]=v.weights[j];sum+=v.weights[j];count++;}
  if(sum<=1e-7f){k.bones={int(out.identityBone),-1,-1,-1};k.weights={1,0,0,0};count=1;}
  k.type=count;keys.push_back(k);
 }
 std::stable_sort(keys.begin(),keys.end(),[](const Key& a,const Key& b){if(a.type!=b.type)return a.type<b.type;return a.bones<b.bones;});
 auto pad=[&](){out.order.push_back(-1);for(auto* a:{&out.px,&out.py,&out.pz,&out.nx,&out.ny,&out.nz,&out.tx,&out.ty,&out.tz,&out.w0,&out.w1,&out.w2,&out.w3})a->push_back(0);};
 for(size_t i=0;i<keys.size();){
  size_t j=i;while(j<keys.size()&&keys[j].type==keys[i].type&&keys[j].bones==keys[i].bones)j++;
  Model::SkinLayout::Group group;group.influences=keys[i].type;group.bones=keys[i].bones;group.first=unsigned(out.order.size());
  for(size_t k=i;k<j;k++){const auto& key=keys[k];const auto& v=m.vertices[key.index];out.sorted[key.index]=int(out.order.size());out.order.push_back(int(key.index));
   out.px.push_back(v.position.x());out.py.push_back(v.position.y());out.pz.push_back(v.position.z());out.nx.push_back(v.normal.x());out.ny.push_back(v.normal.y());out.nz.push_back(v.normal.z());
   auto t=key.index<m.tangents.size()?m.tangents[key.index]:btVector3(1,0,0);out.tx.push_back(t.x());out.ty.push_back(t.y());out.tz.push_back(t.z());
   out.w0.push_back(key.weights[0]);out.w1.push_back(key.weights[1]);out.w2.push_back(key.weights[2]);out.w3.push_back(key.weights[3]);}
  while(out.order.size()%Lanes)pad();
  group.count=unsigned(out.order.size())-group.first;out.groups.push_back(group);i=j;
 }
 // Balanced chunks for the worker pool, never splitting a group.
 constexpr unsigned chunkVertices=8192;unsigned begin=0,accumulated=0;
 for(unsigned g=0;g<out.groups.size();g++){accumulated+=out.groups[g].count;if(accumulated>=chunkVertices){out.chunks.push_back({begin,g+1});begin=g+1;accumulated=0;}}
 if(begin<out.groups.size())out.chunks.push_back({begin,unsigned(out.groups.size())});
}
}
std::shared_ptr<const Model::SkinLayout> Model::skinLayout()const{std::lock_guard lock(skinMutex);if(!skin){auto layout=std::make_shared<SkinLayout>();buildSkinLayout(*this,*layout);skin=std::move(layout);}return skin;}
void Model::invalidateSkinLayout(){{std::lock_guard lock(skinMutex);skin.reset();}std::lock_guard lock(gpuMutex);gpu.reset();}
namespace {
void buildGpuSkin(const Model& m,GpuSkin& out){
 size_t n=m.vertices.size(),bones=m.bones.size();const unsigned identity=unsigned(bones);out.palette=unsigned(bones+1);
 out.cpuVertex.assign(n,0);out.bones.assign(n,{identity,identity,identity});out.weights.assign(n,{1,0,0});
 // Deterministic test poses: every bone rotated about a pseudo-random axis by
 // up to 25, 45 and 90 degrees, four of each. mmdhl_skinning_analysis shows the
 // kept vertices then stay within about twice the limit on unseen poses (two
 // poses underestimated it eightfold).
 std::vector<std::vector<btTransform>> poses;uint32_t seed=12345;
 for(int repeat=0;repeat<4;repeat++)for(float degrees:{25.f,45.f,90.f}){
  std::vector<btTransform> manual(bones,btTransform::getIdentity());
  auto next=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24);};
  for(auto& t:manual){btVector3 axis(next()*2-1,next()*2-1,next()*2-1);if(axis.length2()<1e-6f)axis={0,1,0};t.setRotation(btQuaternion(axis.normalized(),(next()*2-1)*degrees*SIMD_RADS_PER_DEG));}
  std::vector<btTransform> local,global,skin,effective;evaluatePose(m,manual,std::vector<float>(m.morphs.size(),0.f),nullptr,nullptr,nullptr,local,global,skin,effective);poses.push_back(std::move(skin));
 }
 std::vector<unsigned> four;
 for(size_t i=0;i<n;i++){const auto& v=m.vertices[i];
  if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF||v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF){out.cpuVertex[i]=1;continue;}
  std::array<std::pair<float,unsigned>,4> influences{};int count=0;
  for(int k=0;k<4;k++)if(v.weights[k]!=0)influences[count++]={v.weights[k],v.bones[k]>=0&&size_t(v.bones[k])<bones?unsigned(v.bones[k]):identity};
  if(count==0){influences[0]={1.f,identity};count=1;}
  std::sort(influences.begin(),influences.begin()+count,[](auto& a,auto& b){return a.first>b.first;});
  int kept=std::min(count,3);float sum=0;for(int k=0;k<kept;k++)sum+=influences[k].first;
  if(!(sum>1e-7f)){out.cpuVertex[i]=1;continue;}
  for(int k=0;k<3;k++){out.bones[i][k]=k<kept?influences[k].second:influences[0].second;out.weights[i][k]=k<kept?influences[k].first/sum:0.f;}
  if(v.type==NANOEM_MODEL_VERTEX_TYPE_BDEF4){if(count==4)out.fourWeight++;four.push_back(unsigned(i));}
 }
 // BDEF4 may carry a fourth weight or weights not summing to one; the shader
 // renormalises (third weight = 1 - w0 - w1). Keep those whose result moves.
 std::vector<uint8_t> moved(four.size(),0);
 parallelFor(four.size(),512,[&](size_t begin,size_t end){for(size_t f=begin;f<end;f++){size_t i=four[f];const auto& v=m.vertices[i];
  Vertex reduced=v;reduced.bones={-1,-1,-1,-1};reduced.weights={0,0,0,0};
  for(int k=0;k<3;k++)if(out.weights[i][k]>0){reduced.bones[k]=out.bones[i][k]<identity?int(out.bones[i][k]):-1;reduced.weights[k]=out.weights[i][k];}
  float error=0;for(auto& pose:poses){error=std::max(error,(skinPosition(reduced,pose)-skinPosition(v,pose)).length());if(error>GpuSkin::MaxDropError)break;}
  moved[f]=error>GpuSkin::MaxDropError;}});
 for(size_t f=0;f<four.size();f++)if(moved[f]){out.cpuVertex[four[f]]=1;out.droppedWeight++;}
 size_t triangles=m.indices.size()/3;out.cpuTriangle.assign(triangles,0);std::vector<uint8_t> cpu(n,0);
 for(size_t t=0;t<triangles;t++){bool any=false;for(int k=0;k<3;k++)any|=out.cpuVertex[m.indices[t*3+k]]!=0;if(any){out.cpuTriangle[t]=1;for(int k=0;k<3;k++)cpu[m.indices[t*3+k]]=1;}}
 for(auto& group:m.lightOverlaps)for(auto& triangle:group)for(auto index:triangle.vertices)if(index<n)cpu[index]=1;
 for(size_t i=0;i<n;i++)if(cpu[i])out.cpuVertices.push_back(unsigned(i));
 // Bone boxes bound every blended position: a convex combination of the
 // influencing bones' transforms of the (morphed) rest position.
 std::vector<float> reach(n,0.f);
 for(auto morph:m.morphs)if(nanoemModelMorphGetType(morph)==NANOEM_MODEL_MORPH_TYPE_VERTEX){nanoem_rsize_t count=0;auto entries=nanoemModelMorphGetAllVertexMorphObjects(morph,&count);
  for(size_t k=0;k<count;k++){int id=vertexIndex(nanoemModelMorphVertexGetVertexObject(entries[k]));if(id<0||size_t(id)>=n)continue;auto p=nanoemModelMorphVertexGetPosition(entries[k]);reach[size_t(id)]+=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}}
 out.boneBounds.assign(out.palette,{BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT,-BT_LARGE_FLOAT,-BT_LARGE_FLOAT,-BT_LARGE_FLOAT});out.boneReach.assign(out.palette,0.f);
 for(size_t i=0;i<n;i++){if(out.cpuVertex[i])continue;const auto& p=m.vertices[i].position;
  for(int k=0;k<3;k++){if(out.weights[i][k]<=0)continue;auto& box=out.boneBounds[out.bones[i][k]];for(int a=0;a<3;a++){box[a]=std::min(box[a],p[a]-reach[i]);box[a+3]=std::max(box[a+3],p[a]+reach[i]);}
   out.boneReach[out.bones[i][k]]=std::max(out.boneReach[out.bones[i][k]],reach[i]);}}
}
}
// Material morphs and forced opacity change these fields, never the geometry.
static bool sameAppearance(const std::vector<Material>& a,const std::vector<Material>& b){
 if(a.size()!=b.size())return false;
 for(size_t i=0;i<a.size();i++){const auto& x=a[i];const auto& y=b[i];
  if(x.diffuse!=y.diffuse||x.ambient!=y.ambient||x.specular!=y.specular||x.edgeColor!=y.edgeColor||x.alpha!=y.alpha||x.power!=y.power||x.edgeAlpha!=y.edgeAlpha||x.edgeSize!=y.edgeSize||x.textureBlend!=y.textureBlend)return false;}
 return true;
}
std::shared_ptr<const GpuSkin> Model::gpuSkin()const{std::lock_guard lock(gpuMutex);if(!gpu){auto plan=std::make_shared<GpuSkin>();buildGpuSkin(*this,*plan);gpu=std::move(plan);}return gpu;}
void Instance::setMaterialState(std::vector<bool> visible,std::vector<bool> forceOpaque){
 if(visible.size()!=model->materials.size()||forceOpaque.size()!=visible.size())throw std::runtime_error("Invalid material state");
 if(visible==materialVisible&&forceOpaque==materialForceOpaque)return;
 materialVisible=std::move(visible);materialForceOpaque=std::move(forceOpaque);++staticsVersion;poseDirty=true;
}
void Instance::publish(double t){
    auto started=std::chrono::steady_clock::now();
    auto layoutPtr=model->skinLayout();const auto& layout=*layoutPtr;size_t n=model->vertices.size();
    auto result=backSnapshot&&backSnapshot.use_count()==1?backSnapshot:std::make_shared<Snapshot>();
    // A draw queued for Source's render thread may have just released the last
    // other reference; order its reads before this reuse.
    if(result==backSnapshot)std::atomic_thread_fence(std::memory_order_acquire);result->sequence=snapshot?snapshot->sequence+1:1;result->time=t;result->bones=global;
    const auto& weights=expandedMorphs();
    bool forceAlpha=std::any_of(materialForceOpaque.begin(),materialForceOpaque.end(),[](bool v){return v;});
    bool anyMaterialMorph=forceAlpha,anyVertexMorph=false;
    // Only texture and UVA1 morphs write UVs (UVA2-4 reach no shader).
    auto writesUv=[](int type){return type==NANOEM_MODEL_MORPH_TYPE_TEXTURE||type==NANOEM_MODEL_MORPH_TYPE_UVA1;};std::vector<std::pair<unsigned,float>> uvState;
    for(size_t i=0;i<weights.size();i++)if(weights[i]!=0){int type=nanoemModelMorphGetType(model->morphs[i]);anyMaterialMorph|=type==NANOEM_MODEL_MORPH_TYPE_MATERIAL;anyVertexMorph|=type==NANOEM_MODEL_MORPH_TYPE_VERTEX;if(writesUv(type))uvState.emplace_back(unsigned(i),weights[i]);}
    // Materials are copied only while a material morph is active or the buffer is stale.
    if(anyMaterialMorph||!result->materialsPristine||result->materials.size()!=model->materials.size()){result->materials=model->materials;result->materialsPristine=!anyMaterialMorph;}
    // Sparse morph state: restore the vertices touched last time, then apply the current weights.
    bool layoutChanged=morphLayout!=layoutPtr;
    if(layoutChanged){morphX=layout.px;morphY=layout.py;morphZ=layout.pz;morphTouched.clear();morphLayout=layoutPtr;}
    bool morphDirty=!morphTouched.empty()||!scalarMorph.empty();
    for(auto k:morphTouched){morphX[k]=layout.px[k];morphY[k]=layout.py[k];morphZ[k]=layout.pz[k];}morphTouched.clear();scalarMorph.clear();
    // UVs are restored and re-applied only when their morphs' weights change, so a
    // held UV morph keeps the statics (and the idle publish) of an unmorphed model.
    bool uvReset=uvU.size()!=n,uvChanged=uvReset||uvState!=uvWeights;
    if(uvReset){uvU.resize(n);uvV.resize(n);uvE0.resize(n);uvE1.resize(n);for(size_t i=0;i<n;i++){const auto& v=model->vertices[i];uvU[i]=v.uv[0];uvV[i]=v.uv[1];uvE0[i]=v.extra[0][0];uvE1[i]=v.extra[0][1];}uvTouched.clear();}
    if(uvChanged){for(auto i:uvTouched){const auto& v=model->vertices[i];uvU[i]=v.uv[0];uvV[i]=v.uv[1];uvE0[i]=v.extra[0][0];uvE1[i]=v.extra[0][1];}uvTouched.clear();}
    for(size_t i=0;i<weights.size();i++)if(weights[i]!=0){auto morph=model->morphs[i];float w=weights[i];nanoem_rsize_t count=0;int type=nanoemModelMorphGetType(morph);
        if(type==NANOEM_MODEL_MORPH_TYPE_VERTEX){auto entries=nanoemModelMorphGetAllVertexMorphObjects(morph,&count);for(size_t k=0;k<count;k++){int id=vertexIndex(nanoemModelMorphVertexGetVertexObject(entries[k]));if(id<0||size_t(id)>=n)continue;auto p=nanoemModelMorphVertexGetPosition(entries[k]);int s=layout.sorted[id];
            if(s>=0){morphX[s]+=p[0]*w;morphY[s]+=p[1]*w;morphZ[s]+=p[2]*w;morphTouched.push_back(unsigned(s));}
            else scalarMorph.try_emplace(id,btVector3(0,0,0)).first->second+=btVector3(p[0],p[1],p[2])*w;}}
        if(uvChanged&&writesUv(type)){auto entries=nanoemModelMorphGetAllUVMorphObjects(morph,&count);for(size_t k=0;k<count;k++){int id=vertexIndex(nanoemModelMorphUVGetVertexObject(entries[k]));if(id<0||size_t(id)>=n)continue;auto p=nanoemModelMorphUVGetPosition(entries[k]);
            if(type==NANOEM_MODEL_MORPH_TYPE_TEXTURE){uvU[id]+=p[0]*w;uvV[id]+=p[1]*w;}else if(type==NANOEM_MODEL_MORPH_TYPE_UVA1){uvE0[id]+=p[0]*w;uvE1[id]+=p[1]*w;}uvTouched.push_back(unsigned(id));}}
        if(type==NANOEM_MODEL_MORPH_TYPE_MATERIAL){auto entries=nanoemModelMorphGetAllMaterialMorphObjects(morph,&count);for(size_t k=0;k<count;k++){auto e=entries[k];auto target=nanoemModelMorphMaterialGetMaterialObject(e);int index=target?nanoemModelObjectGetIndex(nanoemModelMaterialGetModelObject(target)):-1;bool multiply=nanoemModelMorphMaterialGetOperationType(e)==NANOEM_MODEL_MORPH_MATERIAL_OPERATION_TYPE_MULTIPLY;
            auto scalar=[&](float& out,float value){out=multiply?out*(1+(value-1)*w):out+value*w;};auto color=[&](btVector3& out,const float* value){for(int c=0;c<3;c++)scalar(out[c],value[c]);};
            for(size_t mi=0;mi<result->materials.size();mi++)if(index<0||size_t(index)==mi){auto& mat=result->materials[mi];color(mat.diffuse,nanoemModelMorphMaterialGetDiffuseColor(e));color(mat.ambient,nanoemModelMorphMaterialGetAmbientColor(e));color(mat.specular,nanoemModelMorphMaterialGetSpecularColor(e));color(mat.edgeColor,nanoemModelMorphMaterialGetEdgeColor(e));scalar(mat.alpha,nanoemModelMorphMaterialGetDiffuseOpacity(e));scalar(mat.power,nanoemModelMorphMaterialGetSpecularPower(e));scalar(mat.edgeAlpha,nanoemModelMorphMaterialGetEdgeOpacity(e));scalar(mat.edgeSize,nanoemModelMorphMaterialGetEdgeSize(e));std::array<const float*,3> blends{nanoemModelMorphMaterialGetDiffuseTextureBlend(e),nanoemModelMorphMaterialGetSphereMapTextureBlend(e),nanoemModelMorphMaterialGetToonTextureBlend(e)};for(int b=0;b<3;b++)for(int c=0;c<4;c++)scalar(mat.textureBlend[b][c],blends[b][c]);}
        }}
    }
    for(size_t i=0;i<materialForceOpaque.size();i++)if(materialForceOpaque[i])result->materials[i].alpha=1;
    morphDirty=morphDirty||!morphTouched.empty()||!scalarMorph.empty();
    if(uvChanged){uvWeights=std::move(uvState);++uvVersion;++staticsVersion;}
    // Static attributes live in the 64-byte Source vertex; refill only when the buffer is new or UV morphs changed.
    bool newBuffer=result->vertices.size()!=n;bool rebuildStatics=newBuffer||result->staticsVersion!=staticsVersion;
    result->vertices.resize(n);
    if(rebuildStatics){for(size_t i=0;i<n;i++){auto& d=result->vertices[i];const auto& v=model->vertices[i];d.color=0xffffffffu;d.u=uvU[i];d.v=uvV[i];d.edge=v.edge;d.extra0=uvE0[i];d.extra1=uvE1[i];if(sourceRig&&i<model->tangentSigns.size())d.tw=-model->tangentSigns[i];else{d.tx=1;d.ty=0;d.tz=0;d.tw=1;}}result->staticsVersion=staticsVersion;result->uvVersion=uvVersion;}
    result->minimum=btVector3(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT);result->maximum=-result->minimum;
    // Hardware skinning: the renderer draws most vertices from rest data and the
    // palette below; only GpuSkin::cpuVertices are deformed here. Soft bodies,
    // frozen instances and blocked materials keep CPU skinning.
    const bool gpu=owner&&owner->gpuSkinning&&sourceRig&&!frozen&&!gpuBlocked&&!cpuRequest&&softBodies.empty()&&!(secondary&&secondary->deformsSoft());
    cpuRequest=false;
    std::shared_ptr<const GpuSkin> plan=gpu?model->gpuSkin():nullptr;bool restChanged=false;
    if(gpu){
        // Rest data changes only with vertex/UV morph weights: rewrite the vertices
        // the previous and current weights touch, stamped with a new rest version.
        // Queued draws may be uploading from this rest data on the render thread.
        auto& rest=*gpuRest;
        auto write=[&](size_t i){int s=layout.sorted[i];btVector3 p=s>=0?btVector3(morphX[s],morphY[s],morphZ[s]):model->vertices[i].position;
            if(s<0)if(auto it=scalarMorph.find(int(i));it!=scalarMorph.end())p+=it->second;
            rest.positions[i*3]=p.x();rest.positions[i*3+1]=p.y();rest.positions[i*3+2]=p.z();rest.changed[i]=gpuRestVersion;};
        std::vector<unsigned> touched;touched.reserve(morphTouched.size()+uvTouched.size()+scalarMorph.size());
        for(auto s:morphTouched)touched.push_back(unsigned(layout.order[s]));touched.insert(touched.end(),uvTouched.begin(),uvTouched.end());for(auto& entry:scalarMorph)touched.push_back(unsigned(entry.first));
        if(rest.positions.size()!=n*3||rest.changed.size()!=n||layoutChanged){std::lock_guard lock(rest.mutex);++gpuRestVersion;rest.positions.resize(n*3);rest.changed.resize(n);for(size_t i=0;i<n;i++)write(i);restChanged=true;}
        else if(weights!=gpuMorphWeights){std::lock_guard lock(rest.mutex);++gpuRestVersion;for(auto i:gpuTouched)write(i);for(auto i:touched)write(i);restChanged=true;}
        gpuMorphWeights=weights;gpuTouched=std::move(touched);
    }
    // One affine matrix per bone folds the MMD-to-Source basis, the model scale and the inch conversion.
    size_t boneCount=skin.size();palette.resize((boneCount+1)*12);
    {auto c=basis();float unit=scale/Inch;btMatrix3x3 placementBasis=placement.getBasis();btVector3 placementOrigin=placement.getOrigin()/Inch;
     for(size_t b=0;b<=boneCount;b++){btTransform t=b<boneCount?skin[b]:btTransform::getIdentity();btMatrix3x3 rotation=placementBasis*c*t.getBasis()*unit;btVector3 origin=placementBasis*(c*t.getOrigin())*unit+placementOrigin;float* out=&palette[b*12];for(int r=0;r<3;r++){out[r*4]=rotation[r].x();out[r*4+1]=rotation[r].y();out[r*4+2]=rotation[r].z();out[r*4+3]=origin[r];}}}
    // Record which palette bones moved since the previous publish, so the
    // renderer uploads only the vertex spans they influence.
    {uint64_t seq=result->sequence;changedBones=0;
     if(boneChangedState.size()!=boneCount+1||previousPalette.size()!=palette.size()){boneChangedState.assign(boneCount+1,seq);previousPalette=palette;allChangedState=seq;changedBones=unsigned(boneCount+1);}
     else for(size_t b=0;b<=boneCount;b++)if(std::memcmp(&palette[b*12],&previousPalette[b*12],12*sizeof(float))!=0){boneChangedState[b]=seq;std::memcpy(&previousPalette[b*12],&palette[b*12],12*sizeof(float));changedBones++;}
     bool soft=!softBodies.empty()||(secondary&&secondary->deformsSoft());
     // Under hardware skinning only a morph weight change alters vertex data;
     // active but unchanged morphs no longer force full uploads.
     if(gpu)fullChangeReasons=(restChanged?1u:0u)|(layoutChanged?8u:0u)|(newBuffer?16u:0u);
     else fullChangeReasons=(morphDirty?1u:0u)|(rebuildStatics?2u:0u)|(soft?4u:0u)|(layoutChanged?8u:0u)|(newBuffer?16u:0u);
     fullChange=fullChangeReasons!=0;if(fullChange)allChangedState=seq;
     if(fullChange||changedBones)geometryState=seq;
     result->boneChanged=boneChangedState;result->allChanged=allChangedState;result->geometry=geometryState;
     // Unchanged geometry is reused, but a material-only change (a material morph,
     // forced opacity) still needs a snapshot that carries it.
     if(snapshot&&snapshot->gpu==gpu&&changedBones==0&&!fullChange&&snapshot->vertices.size()==n&&sameAppearance(snapshot->materials,result->materials)){poseDirty=false;idleFrames++;deformMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return;}}
    rotationPalette.resize(boneCount);dualPalette.resize(boneCount);
    for(size_t i=0;i<boneCount;i++){auto q=skin[i].getRotation();auto p=skin[i].getOrigin();rotationPalette[i]=q;dualPalette[i]=(btQuaternion(p.x(),p.y(),p.z(),0)*q)*.5f;}
    auto rotation=[&](int i){return i>=0?rotationPalette[size_t(i)]:btQuaternion::getIdentity();};
    bool tangents=sourceRig&&model->tangents.size()==n;
    // Degenerate normals and tangents fall back to the MMD up axis, expressed in Source space like the scalar path.
    const btVector3 fallbackAxis=placement.getBasis()*toSource(btVector3(0,1,0));
    struct Bounds {btVector3 lo{BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT},hi{-BT_LARGE_FLOAT,-BT_LARGE_FLOAT,-BT_LARGE_FLOAT};};
    auto store=[&](DrawVertex& d,float x,float y,float z,float nx,float ny,float nz,float tx,float ty,float tz,Bounds& bounds){d.x=x;d.y=y;d.z=z;d.nx=nx;d.ny=ny;d.nz=nz;if(tangents){d.tx=tx;d.ty=ty;d.tz=tz;}btVector3 p(x,y,z);bounds.lo.setMin(p);bounds.hi.setMax(p);};
    // Scalar reference for SDEF/QDEF vertices and for processors without AVX2.
    auto deformScalar=[&](size_t i,Bounds& bounds){auto& d=result->vertices[i];const auto& v=model->vertices[i];
        btVector3 position=v.position;int s=layout.sorted[i];if(s>=0)position=btVector3(morphX[s],morphY[s],morphZ[s]);else if(auto it=scalarMorph.find(int(i));it!=scalarMorph.end())position+=it->second;
        auto tangent=tangents?model->tangents[i]:btVector3(1,0,0);SkinnedFrame frame;
        if(v.type==NANOEM_MODEL_VERTEX_TYPE_SDEF){
            auto& a=matrix(v,0,skin);auto& b=matrix(v,1,skin);float w=v.weights[0];auto rw=v.r0*w+v.r1*(1-w);
            auto q=rotation(v.bones[0]).slerp(rotation(v.bones[1]),1-w);
            frame={quatRotate(q,position-v.c)+(a*(v.c+(v.r0-rw)*.5f))*w+(b*(v.c+(v.r1-rw)*.5f))*(1-w),quatRotate(q,v.normal),quatRotate(q,tangent)};
        }else if(v.type==NANOEM_MODEL_VERTEX_TYPE_QDEF){
            btQuaternion real(0,0,0,0),dual(0,0,0,0),reference=rotation(v.bones[0]);
            for(int k=0;k<4;k++)if(v.weights[k]>0){int b=v.bones[k];auto q=rotation(b);float w=v.weights[k]*(q.dot(reference)<0?-1.f:1.f);real+=q*w;if(b>=0)dual+=dualPalette[b]*w;}
            float length=real.length();btTransform transform=btTransform::getIdentity();if(length>=1e-7f){real/=length;dual/=length;dual-=real*real.dot(dual);auto p=(dual*real.inverse())*2;transform=btTransform(real,btVector3(p.x(),p.y(),p.z()));}
            frame={transform*position,transform.getBasis()*v.normal,transform.getBasis()*tangent};
        }else{
            frame.position=frame.normal=frame.tangent=btVector3(0,0,0);float sum=0;
            for(int k=0;k<4;k++)if(v.weights[k]!=0){const auto& transform=matrix(v,k,skin);float w=v.weights[k];frame.position+=(transform*position)*w;frame.normal+=(transform.getBasis()*v.normal)*w;frame.tangent+=(transform.getBasis()*tangent)*w;sum+=w;}
            if(sum<=1e-7f)frame.position=position;
            if(frame.tangent.length2()<=1e-12f)frame.tangent=btVector3(0,1,0);
        }
        frame.normal=frame.normal.length2()>1e-12f?frame.normal.normalized():btVector3(0,1,0);
        auto p=placement*(toSource(frame.position)*scale)/Inch;auto nrm=placement.getBasis()*toSource(frame.normal);
        auto tg=placement.getBasis()*toSource(frame.tangent);tg-=nrm*nrm.dot(tg);if(tg.length2()>1e-9f)tg.normalize();
        store(d,p.x(),p.y(),p.z(),nrm.x(),nrm.y(),nrm.z(),tg.x(),tg.y(),tg.z(),bounds);
    };
    if(gpu){
        // Vertices of CPU triangles that the shaders could skin use the same three
        // renormalised weights as the GPU, so shared edges meet exactly.
        auto deformReduced=[&](size_t i,Bounds& bounds){const auto& v=model->vertices[i];int s=layout.sorted[i];btVector3 rest=s>=0?btVector3(morphX[s],morphY[s],morphZ[s]):v.position;
            btVector3 t=tangents?model->tangents[i]:btVector3(1,0,0),p(0,0,0),q(0,0,0),r(0,0,0);
            for(int k=0;k<3;k++){float w=plan->weights[i][k];if(w==0)continue;const float* m=&palette[size_t(plan->bones[i][k])*12];
                for(int a=0;a<3;a++){const float* row=m+a*4;p[a]+=w*(row[0]*rest.x()+row[1]*rest.y()+row[2]*rest.z()+row[3]);q[a]+=w*(row[0]*v.normal.x()+row[1]*v.normal.y()+row[2]*v.normal.z());r[a]+=w*(row[0]*t.x()+row[1]*t.y()+row[2]*t.z());}}
            q=q.length2()>1e-12f?q.normalized():fallbackAxis;if(r.length2()<=1e-12f)r=fallbackAxis;r-=q*q.dot(r);if(r.length2()>1e-9f)r.normalize();
            store(result->vertices[i],p.x(),p.y(),p.z(),q.x(),q.y(),q.z(),r.x(),r.y(),r.z(),bounds);};
        const auto& subset=plan->cpuVertices;size_t chunks=(subset.size()+2047)/2048;std::vector<Bounds> bounds(chunks+1);
        parallelFor(chunks,1,[&](size_t begin,size_t end){for(size_t c=begin;c<end;c++)for(size_t k=c*2048;k<std::min(subset.size(),(c+1)*2048);k++){size_t i=subset[k];if(plan->cpuVertex[i])deformScalar(i,bounds[c]);else deformReduced(i,bounds[c]);}});
        // Every GPU-skinned position lies in the union of its bones' transformed rest boxes.
        // The boxes hold each vertex morph once; weights reach 2 (and more through
        // groups), and |sum w*offset| <= max|w| * reach, so grow by the excess.
        float morphScale=1;for(size_t i=0;i<weights.size()&&i<model->morphs.size();i++)if(weights[i]!=0&&nanoemModelMorphGetType(model->morphs[i])==NANOEM_MODEL_MORPH_TYPE_VERTEX)morphScale=std::max(morphScale,std::fabs(weights[i]));
        auto& all=bounds.back();
        for(size_t b=0;b<plan->boneBounds.size()&&b<=boneCount;b++){const auto& box=plan->boneBounds[b];if(box[0]>box[3])continue;const float* m=&palette[b*12];
            float extra=b<plan->boneReach.size()?(morphScale-1)*plan->boneReach[b]:0.f;
            btVector3 center((box[0]+box[3])*.5f,(box[1]+box[4])*.5f,(box[2]+box[5])*.5f),half((box[3]-box[0])*.5f+extra,(box[4]-box[1])*.5f+extra,(box[5]-box[2])*.5f+extra),c,e;
            for(int a=0;a<3;a++){const float* row=m+a*4;c[a]=row[0]*center.x()+row[1]*center.y()+row[2]*center.z()+row[3];e[a]=std::abs(row[0])*half.x()+std::abs(row[1])*half.y()+std::abs(row[2])*half.z();}
            all.lo.setMin(c-e);all.hi.setMax(c+e);}
        for(auto& b:bounds){result->minimum.setMin(b.lo);result->maximum.setMax(b.hi);}
        result->gpu=true;result->restVersion=gpuRestVersion;result->palette=palette;
        backSnapshot=std::const_pointer_cast<Snapshot>(snapshot);snapshot=result;poseDirty=false;
        deformMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return;
    }
    result->gpu=false;result->palette.clear();
    const bool simd=avx2FmaAvailable();
    size_t simdChunks=simd?layout.chunks.size():0,scalarChunks=(layout.scalar.size()+4095)/4096,fallbackChunks=simd?0:(layout.order.size()+4095)/4096;
    size_t chunkCount=simdChunks+scalarChunks+fallbackChunks;std::vector<Bounds> bounds(chunkCount);
    parallelFor(chunkCount,1,[&](size_t begin,size_t end){for(size_t chunk=begin;chunk<end;chunk++){auto& bound=bounds[chunk];
        if(chunk<simdChunks){
#if defined(__AVX2__)||defined(_MSC_VER)
            auto [g0,g1]=layout.chunks[chunk];alignas(32) float outX[Lanes],outY[Lanes],outZ[Lanes],outNx[Lanes],outNy[Lanes],outNz[Lanes],outTx[Lanes],outTy[Lanes],outTz[Lanes];
            const __m256 tiny=_mm256_set1_ps(1e-12f),tinyTangent=_mm256_set1_ps(1e-9f),one=_mm256_set1_ps(1.f),zero=_mm256_setzero_ps();
            const __m256 fbx=_mm256_set1_ps(fallbackAxis.x()),fby=_mm256_set1_ps(fallbackAxis.y()),fbz=_mm256_set1_ps(fallbackAxis.z());
            for(unsigned g=g0;g<g1;g++){const auto& group=layout.groups[g];
                const float* m[4];for(int k=0;k<4;k++)m[k]=&palette[size_t(group.bones[k]>=0?group.bones[k]:0)*12];
                const float* wv[4]={layout.w0.data(),layout.w1.data(),layout.w2.data(),layout.w3.data()};
                for(unsigned k=group.first;k<group.first+group.count;k+=Lanes){
                    __m256 vx=_mm256_loadu_ps(&morphX[k]),vy=_mm256_loadu_ps(&morphY[k]),vz=_mm256_loadu_ps(&morphZ[k]);
                    __m256 nx=_mm256_loadu_ps(&layout.nx[k]),ny=_mm256_loadu_ps(&layout.ny[k]),nz=_mm256_loadu_ps(&layout.nz[k]);
                    __m256 tx=_mm256_loadu_ps(&layout.tx[k]),ty=_mm256_loadu_ps(&layout.ty[k]),tz=_mm256_loadu_ps(&layout.tz[k]);
                    __m256 px=zero,py=zero,pz=zero,qx=zero,qy=zero,qz=zero,rx=zero,ry=zero,rz=zero;
                    for(int i=0;i<group.influences;i++){__m256 wi=_mm256_loadu_ps(&wv[i][k]);const float* a=m[i];
                        __m256 a00=_mm256_broadcast_ss(a),a01=_mm256_broadcast_ss(a+1),a02=_mm256_broadcast_ss(a+2),a03=_mm256_broadcast_ss(a+3);
                        __m256 a10=_mm256_broadcast_ss(a+4),a11=_mm256_broadcast_ss(a+5),a12=_mm256_broadcast_ss(a+6),a13=_mm256_broadcast_ss(a+7);
                        __m256 a20=_mm256_broadcast_ss(a+8),a21=_mm256_broadcast_ss(a+9),a22=_mm256_broadcast_ss(a+10),a23=_mm256_broadcast_ss(a+11);
                        px=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a00,vx,_mm256_fmadd_ps(a01,vy,_mm256_fmadd_ps(a02,vz,a03))),px);
                        py=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a10,vx,_mm256_fmadd_ps(a11,vy,_mm256_fmadd_ps(a12,vz,a13))),py);
                        pz=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a20,vx,_mm256_fmadd_ps(a21,vy,_mm256_fmadd_ps(a22,vz,a23))),pz);
                        qx=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a00,nx,_mm256_fmadd_ps(a01,ny,_mm256_mul_ps(a02,nz))),qx);
                        qy=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a10,nx,_mm256_fmadd_ps(a11,ny,_mm256_mul_ps(a12,nz))),qy);
                        qz=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a20,nx,_mm256_fmadd_ps(a21,ny,_mm256_mul_ps(a22,nz))),qz);
                        rx=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a00,tx,_mm256_fmadd_ps(a01,ty,_mm256_mul_ps(a02,tz))),rx);
                        ry=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a10,tx,_mm256_fmadd_ps(a11,ty,_mm256_mul_ps(a12,tz))),ry);
                        rz=_mm256_fmadd_ps(wi,_mm256_fmadd_ps(a20,tx,_mm256_fmadd_ps(a21,ty,_mm256_mul_ps(a22,tz))),rz);
                    }
                    __m256 nlen=_mm256_fmadd_ps(qx,qx,_mm256_fmadd_ps(qy,qy,_mm256_mul_ps(qz,qz)));__m256 nvalid=_mm256_cmp_ps(nlen,tiny,_CMP_GT_OQ);__m256 ninv=_mm256_div_ps(one,_mm256_sqrt_ps(_mm256_max_ps(nlen,tiny)));
                    qx=_mm256_blendv_ps(fbx,_mm256_mul_ps(qx,ninv),nvalid);qy=_mm256_blendv_ps(fby,_mm256_mul_ps(qy,ninv),nvalid);qz=_mm256_blendv_ps(fbz,_mm256_mul_ps(qz,ninv),nvalid);
                    __m256 tlen=_mm256_fmadd_ps(rx,rx,_mm256_fmadd_ps(ry,ry,_mm256_mul_ps(rz,rz)));__m256 tvalid=_mm256_cmp_ps(tlen,tiny,_CMP_GT_OQ);
                    rx=_mm256_blendv_ps(fbx,rx,tvalid);ry=_mm256_blendv_ps(fby,ry,tvalid);rz=_mm256_blendv_ps(fbz,rz,tvalid);
                    __m256 dot=_mm256_fmadd_ps(qx,rx,_mm256_fmadd_ps(qy,ry,_mm256_mul_ps(qz,rz)));rx=_mm256_fnmadd_ps(qx,dot,rx);ry=_mm256_fnmadd_ps(qy,dot,ry);rz=_mm256_fnmadd_ps(qz,dot,rz);
                    __m256 tlen2=_mm256_fmadd_ps(rx,rx,_mm256_fmadd_ps(ry,ry,_mm256_mul_ps(rz,rz)));__m256 tnorm=_mm256_cmp_ps(tlen2,tinyTangent,_CMP_GT_OQ);__m256 tinv=_mm256_div_ps(one,_mm256_sqrt_ps(_mm256_max_ps(tlen2,tinyTangent)));
                    rx=_mm256_blendv_ps(rx,_mm256_mul_ps(rx,tinv),tnorm);ry=_mm256_blendv_ps(ry,_mm256_mul_ps(ry,tinv),tnorm);rz=_mm256_blendv_ps(rz,_mm256_mul_ps(rz,tinv),tnorm);
                    _mm256_store_ps(outX,px);_mm256_store_ps(outY,py);_mm256_store_ps(outZ,pz);_mm256_store_ps(outNx,qx);_mm256_store_ps(outNy,qy);_mm256_store_ps(outNz,qz);_mm256_store_ps(outTx,rx);_mm256_store_ps(outTy,ry);_mm256_store_ps(outTz,rz);
                    for(unsigned l=0;l<Lanes;l++){int index=layout.order[k+l];if(index<0)continue;store(result->vertices[size_t(index)],outX[l],outY[l],outZ[l],outNx[l],outNy[l],outNz[l],outTx[l],outTy[l],outTz[l],bound);}
                }
            }
#endif
        }else if(chunk<simdChunks+scalarChunks){size_t c=chunk-simdChunks;for(size_t k=c*4096;k<std::min(layout.scalar.size(),(c+1)*4096);k++)deformScalar(layout.scalar[k],bound);}
        else{size_t c=chunk-simdChunks-scalarChunks;for(size_t k=c*4096;k<std::min(layout.order.size(),(c+1)*4096);k++){int index=layout.order[k];if(index>=0)deformScalar(size_t(index),bound);}}
    }});
    for(auto& b:bounds){result->minimum.setMin(b.lo);result->maximum.setMax(b.hi);}
    for(auto& soft:softBodies){for(size_t j=0;j<soft.vertexIndices.size();j++){auto& node=soft.body->m_nodes[int(j)];auto p=node.m_x/Inch;auto& d=result->vertices[soft.vertexIndices[j]];d.x=p.x();d.y=p.y();d.z=p.z();d.nx=node.m_n.x();d.ny=node.m_n.y();d.nz=node.m_n.z();result->minimum.setMin(p);result->maximum.setMax(p);}}
    if(secondary)secondary->deformSoft(*result);
    backSnapshot=std::const_pointer_cast<Snapshot>(snapshot);snapshot=result;poseDirty=false;
    deformMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
} // namespace mmd
