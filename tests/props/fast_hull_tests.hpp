#include <random>
void fastHullTests(){
    check(parseOptions(Json::object()).collision=="hull"&&Options{}.collision=="hull","fast single hull is not the default");
    auto box=fixture();auto h=makeFastHull(box);
    check(h.points.size()==8&&h.indices.size()==36,"box support hull is not an eight-corner convex");
    check(encode(box)==encode(fixture()),"collision construction changed visual geometry");
    std::mt19937 random(401);std::uniform_real_distribution<float> sample(-1,1);
    for(int trial=0;trial<30;++trial){
        auto a=fixture();a.vertices.clear();a.indices.clear();
        const float scale=trial%3==0?.01f:trial%3==1?1.f:500.f;
        const Vec offset=trial%2?Vec{12000,-4000,6000}:Vec{};
        for(unsigned i=0;i<300;++i){
            Vertex v;v.pos=Vec{sample(random)*scale,sample(random)*scale,trial%5?sample(random)*scale:0}+offset;
            a.vertices.push_back(v);a.indices.push_back(i);
        }
        if(trial%3==0&&trial%2){rejects([&]{makeFastHull(a);},"precision-impossible local coordinates accepted");continue;}
        Hull hull,repeated;
        try{hull=makeFastHull(a);repeated=makeFastHull(a);}
        catch(const std::exception& e){throw std::runtime_error("Fast hull trial "+std::to_string(trial)+": "+e.what());}
        check(hull.points.size()<=64&&hull.indices.size()<=384,"fast hull exceeds the network/physics budgets");
        check(hullJson({hull})==hullJson({repeated}),"fast hull is nondeterministic");
        Vec center{};for(auto p:hull.points)center=center+p;center=center*(1.f/hull.points.size());
        bool contains=true;
        for(size_t i=0;i<hull.indices.size();i+=3){
            auto p=hull.points[hull.indices[i]],q=hull.points[hull.indices[i+1]],r=hull.points[hull.indices[i+2]];
            auto n=(q-p).cross(r-p).normalized();if(n.dot(center-p)>0)n=n*-1;
            for(auto v:a.vertices)if(n.dot(v.pos-p)>std::max(.004f,scale*.001f))contains=false;
        }
        check(contains,"fast hull excludes source vertices");
    }
    auto invalid=box;invalid.vertices[0].pos.x=NAN;
    rejects([&]{makeFastHull(invalid);},"fast hull accepts nonfinite geometry");
    invalid=box;invalid.indices[0]=100;
    rejects([&]{makeFastHull(invalid);},"fast hull accepts invalid indices");
    invalid=box;for(auto& v:invalid.vertices)v.pos={};
    rejects([&]{makeFastHull(invalid);},"point-sized model creates an invalid physics hull");
}
