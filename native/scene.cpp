#include "scene.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
namespace mmd {
namespace {
std::atomic<std::shared_ptr<const SceneFrame>> latest;
struct Interest {SceneRegion region;std::chrono::steady_clock::time_point refreshed;};
std::mutex interestMutex;std::unordered_map<uintptr_t,Interest> interests;
}
void noteSceneInterest(uintptr_t consumer,const btVector3& lower,const btVector3& upper){std::lock_guard lock(interestMutex);interests[consumer]={{lower,upper},std::chrono::steady_clock::now()};}
void forgetSceneInterest(uintptr_t consumer){std::lock_guard lock(interestMutex);interests.erase(consumer);}
std::vector<SceneRegion> sceneInterest(){
 std::lock_guard lock(interestMutex);const auto now=std::chrono::steady_clock::now();std::vector<SceneRegion> out;out.reserve(interests.size());
 for(auto it=interests.begin();it!=interests.end();){if(now-it->second.refreshed>std::chrono::seconds(2))it=interests.erase(it);else{out.push_back(it->second.region);++it;}}
 return out;
}
void publishScene(std::shared_ptr<const SceneFrame> frame){latest.store(std::move(frame));}
std::shared_ptr<const SceneFrame> readScene(World* host){if(host){auto local=host->externalScene.load();if(local)return local;}return latest.load();}
}
