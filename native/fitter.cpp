#include "fitter.hpp"
#include "shape_atlas.hpp"
#include <LinearMath/btConvexHullComputer.h>
#include <LinearMath/btGeometryUtil.h>
#include <algorithm>
#include <limits>
namespace mmd {
namespace {
btVector3 vector(const Json& a){return {a[0],a[1],a[2]};}
float quantile(std::vector<float> a,float q){if(a.empty())return 0;size_t k=size_t(q*(a.size()-1));std::nth_element(a.begin(),a.begin()+k,a.end());return a[k];}
std::vector<btVector3> sample(std::vector<btVector3> points,size_t cap){
 if(points.size()<=cap)return points;
 std::vector<btVector3> out;std::vector<float> distances(points.size(),BT_LARGE_FLOAT);size_t at=0;
 while(out.size()<cap){out.push_back(points[at]);float best=-1;for(size_t i=0;i<points.size();i++){distances[i]=std::min(distances[i],(points[i]-out.back()).length2());if(distances[i]>best){best=distances[i];at=i;}}}
 return out;
}
}
const std::string& shapeAtlasHash(){static const auto value=hash(std::span(reinterpret_cast<const unsigned char*>(ShapeAtlas),sizeof(ShapeAtlas)-1));return value;}
void convexTopology(ConvexFit& fit){
 if(fit.vertices.size()<4)throw std::runtime_error("Collision region has fewer than four points");
 btVector3 lo(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT),hi=-lo;
 for(auto p:fit.vertices){for(int k=0;k<3;k++)if(!std::isfinite(p[k]))throw std::runtime_error("Non-finite collision point");lo.setMin(p);hi.setMax(p);}
 const float diameter=std::max((hi-lo).length(),.001f),weld=std::max(1e-4f,diameter*1e-4f);
 std::vector<btVector3> unique;
 for(auto p:fit.vertices){bool duplicate=false;for(auto v:unique)if((p-v).length2()<weld*weld){duplicate=true;break;}if(!duplicate)unique.push_back(p);}
 fit.repaired|=unique.size()!=fit.vertices.size();fit.vertices=sample(std::move(unique),64);
 bool valid=false;for(int attempt=0;attempt<66;attempt++){
  btConvexHullComputer hull;
  if(fit.vertices.size()>=4)hull.compute(&fit.vertices[0][0],sizeof(btVector3),int(fit.vertices.size()),0,0);
  std::vector<btVector3> vertices;for(int i=0;i<hull.vertices.size();i++)vertices.push_back(hull.vertices[i]);
  Json faces=Json::array();int collapse=-1;double volume=0;std::map<std::pair<int,int>,int> edges;
  btVector3 center(0,0,0);for(auto p:vertices)center+=p;if(!vertices.empty())center/=float(vertices.size());
  for(int i=0;i<hull.faces.size();i++){
   std::vector<int> face;const auto* first=&hull.edges[hull.faces[i]];auto edge=first;
   do{face.push_back(edge->getSourceVertex());edge=edge->getNextEdgeOfFace();}while(edge!=first);
   btVector3 normal(0,0,0);auto a=vertices[face[0]];
   for(size_t j=1;j+1<face.size();j++)normal+=(vertices[face[j]]-a).cross(vertices[face[j+1]]-a);
   if(normal.length2()<std::max(1e-10f,diameter*diameter*diameter*diameter*1e-16f)){
    float shortest=BT_LARGE_FLOAT;
    for(size_t j=0;j<face.size();j++){int a=face[j],b=face[(j+1)%face.size()];float d=(vertices[a]-vertices[b]).length2();if(d<shortest){shortest=d;collapse=b;}}
   }
   if(normal.dot(a-center)<0)std::reverse(face.begin(),face.end());
   for(size_t j=1;j+1<face.size();j++)volume+=(vertices[face[0]]-center).dot((vertices[face[j]]-center).cross(vertices[face[j+1]]-center))/6.;
   for(size_t j=0;j<face.size();j++)edges[std::minmax(face[j],face[(j+1)%face.size()])]++;
   faces.push_back(face);
  }
  bool closed=faces.size()>=4&&vertices.size()>=4&&volume>double(diameter)*diameter*diameter*1e-8;
  for(auto [edge,count]:edges)closed&=count==2;
  if(closed&&collapse<0){fit.vertices=std::move(vertices);fit.faces=std::move(faces);valid=true;break;}
  fit.repaired=true;
  if(closed&&collapse>=0&&vertices.size()>4){vertices.erase(vertices.begin()+collapse);fit.vertices=std::move(vertices);continue;}
  if(fit.fallback)throw std::runtime_error("Cannot construct a closed collision hull");
  fit.fallback=true;fit.vertices.clear();auto c=(lo+hi)*.5f,e=(hi-lo)*.5f;
  for(int k=0;k<3;k++)e[k]=std::max(e[k],diameter*.025f);
  for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1})fit.vertices.push_back(c+e*btVector3(float(x),float(y),float(z)));
 }
 if(!valid)throw std::runtime_error("Collision hull repair did not converge");
 lo={BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT};hi=-lo;for(auto p:fit.vertices){lo.setMin(p);hi.setMax(p);}fit.center=(lo+hi)*.5f;fit.extent=(hi-lo)*.5f;
}
ConvexFit fitBody(const std::string& name,const std::vector<btVector3>& input,float stature,float length){
 static const auto atlas=Json::parse(ShapeAtlas);const auto& prior=atlas["bodies"].at(name);
 ConvexFit fit;float unit=std::max(stature,.01f);auto center=vector(prior["center"])*unit,extent=vector(prior["extent"])*unit;
 auto centerLo=vector(prior["centerLow"])*unit,centerHi=vector(prior["centerHigh"])*unit;
 auto extentLo=vector(prior["extentLow"])*unit,extentHi=vector(prior["extentHigh"])*unit;
 std::vector<btVector3> points;points.reserve(input.size());
 for(auto p:input){bool keep=true;for(int k=0;k<3;k++)if(p[k]<centerLo[k]-extentHi[k]*1.4f||p[k]>centerHi[k]+extentHi[k]*1.4f)keep=false;
  if(length>0&&(p.x()<-.35f*length||p.x()>length*1.2f))keep=false;
  if(keep)points.push_back(p);
 }
 fit.outliers=input.empty()?1.f:1.f-float(points.size())/float(input.size());
 fit.features={length/unit};
 for(int k=0;k<3;k++){std::vector<float> values;for(auto p:points)values.push_back(p[k]/unit);for(float q:{.02f,.25f,.5f,.75f,.98f})fit.features.push_back(quantile(values,q));}
 auto featureScale=vector(prior["extent"])*unit;
 for(size_t i=0;i<atlas["directions"].size()/2;i++){auto n=vector(atlas["directions"][i]);std::vector<float> values;for(auto p:points)values.push_back((p/featureScale).dot(n));for(float q:{.005f,.5f,.995f})fit.features.push_back(quantile(values,q));}
 if(points.size()>=24&&prior.contains("supportRegression")&&prior["supportRegression"]["mean"].size()==fit.features.size()){
  // A small, fixed linear model estimates anatomical supporting planes from
  // weighted surface quantiles. Coefficients use calibration families only.
  // It is an empirical shape prior, with no runtime corpus or ML dependency.
  const auto& regression=prior["supportRegression"];auto mean=regression["mean"].get<std::vector<float>>(),stddev=regression["std"].get<std::vector<float>>();
  btAlignedObjectArray<btVector3> planes;std::vector<float> supports;
  for(size_t i=0;i<atlas["directions"].size();i++){float support=regression["intercept"][i];for(size_t k=0;k<fit.features.size();k++)support+=regression["coefficients"][i][k].get<float>()*std::clamp((fit.features[k]-mean[k])/stddev[k],-5.f,5.f);
   support=std::clamp(support,regression["low"][i].get<float>(),regression["high"][i].get<float>());supports.push_back(support);auto n=vector(atlas["directions"][i])/vector(prior["supportScale"]);float magnitude=n.length();n/=magnitude;n.setW(-support*unit/magnitude);planes.push_back(n);
  }
  btAlignedObjectArray<btVector3> vertices;btGeometryUtil::getVerticesFromPlaneEquations(planes,vertices);
  if(vertices.size()>=4){
   for(int i=0;i<vertices.size();i++)fit.vertices.push_back(vertices[i]);convexTopology(fit);fit.method="calibrated_surface_planes";
   if(prior.contains("surfaceTemplates")){
    float best=BT_LARGE_FLOAT;std::vector<btVector3> chosen;
    auto supportScale=vector(prior["supportScale"])*unit;
    for(const auto& item:prior["surfaceTemplates"]){
     std::vector<btVector3> candidate;candidate.reserve(item.size());for(const auto& v:item)candidate.push_back(vector(v)*fit.extent+fit.center);
     float error=0;for(size_t i=0;i<supports.size();i++){auto n=vector(atlas["directions"][i]);float support=-BT_LARGE_FLOAT;for(auto v:candidate)support=std::max(support,(v/supportScale).dot(n));float delta=support-supports[i];error+=delta*delta;}
     if(error<best){best=error;chosen=std::move(candidate);}
    }
    if(!chosen.empty()){fit.vertices=std::move(chosen);fit.method="calibrated_reference_convex";}
   }
   if(length>0)for(auto& v:fit.vertices)v.setX(std::clamp(v.x(),-.35f*length,1.2f*length));convexTopology(fit);
   fit.coverage=1;fit.confidence=std::exp(-2.f*regression["familyCV_RMSE"].get<float>())*std::min(1.f,float(points.size())/80.f)*(1.f-.5f*fit.outliers);return fit;
  }
 }
 if(points.size()>=24){
  for(int k=0;k<3;k++){std::vector<float> values;values.reserve(points.size());for(auto p:points)values.push_back(p[k]);float lo=quantile(values,.03f),hi=quantile(values,.97f);
   center[k]=std::clamp((lo+hi)*.5f,centerLo[k],centerHi[k]);extent[k]=std::clamp((hi-lo)*.5f*.9f,extentLo[k]*.9f,extentHi[k]*1.05f);
  }
 }
 // Compare full normalized surfaces to choose a convex anatomical reference.
 float best=BT_LARGE_FLOAT;const Json* chosen=&prior["templates"][0];auto sampled=sample(points,128);
 for(auto& candidate:prior["templates"]){float score=0;for(auto p:sampled){auto v=(p-center)/extent;float nearest=BT_LARGE_FLOAT;for(auto& j:candidate["shape"]){auto q=vector(j);nearest=std::min(nearest,(v-q).length2());}score+=std::min(nearest,4.f);}if(score<best){best=score;chosen=&candidate;}}
 // Template is the safe candidate for occluded/incomplete geometry. It also
 // supplies inner samples so a sparse material cannot collapse a body to a plane.
 std::vector<btVector3> reference;for(auto& v:(*chosen)["shape"])reference.push_back(center+vector(v)*extent);
 fit.method="anatomical_reference";fit.vertices=reference;
 if(points.size()>=24){
  fit.method="weighted_surface_convex";fit.vertices.clear();
  for(auto p:points){auto v=p-center;bool keep=true;for(int k=0;k<3;k++)if(btFabs(v[k])>extent[k]*1.3f)keep=false;if(!keep)continue;
   v*=.88f;for(int k=0;k<3;k++)v[k]=std::clamp(v[k],-extent[k],extent[k]);fit.vertices.push_back(center+v);
  }
  // Keep characteristic anatomy and joint coverage when painted weights stop short.
  for(auto p:reference)fit.vertices.push_back(center+(p-center)*.88f);
 }
 if(length>0)for(auto& v:fit.vertices)v.setX(std::clamp(v.x(),-.35f*length,1.2f*length));convexTopology(fit);
 size_t covered=0;for(auto p:points){auto q=(p-fit.center)/fit.extent;if(btFabs(q.x())<1.3f&&btFabs(q.y())<1.3f&&btFabs(q.z())<1.3f)covered++;}
 fit.coverage=points.empty()?0:float(covered)/points.size();fit.confidence=std::min(1.f,float(points.size())/80.f)*fit.coverage*(1.f-.5f*fit.outliers);
 return fit;
}
}
