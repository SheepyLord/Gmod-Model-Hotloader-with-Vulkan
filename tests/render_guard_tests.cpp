#include "validation_once.hpp"
#include "render_binaries.hpp"
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <iostream>

int main() {
    if(!mmd::supportedRenderBinary(L"engine.dll","626618f94fabefc69e2f321f984cef76e06591271b0725a9bf220a1c0105b3e5")||
       !mmd::supportedRenderBinary(L"materialsystem.dll","daee78fee22c84312f984a60dee5911ff7fe5c0fac07499ac1bd02d388cb832b")||
       !mmd::supportedRenderBinary(L"shaderapidx9.dll","83f11968822f7896f9e666956719497b9ec22ea5535f6bd4a6f6684937f84f7e")||
       mmd::supportedRenderBinary(L"shaderapidx9.dll","daee78fee22c84312f984a60dee5911ff7fe5c0fac07499ac1bd02d388cb832b")||
       mmd::supportedRenderBinary(L"materialsystem.dll","unknown"))return 2;
    // Concurrent/repeated render passes must neither repeat the expensive
    // fingerprint read nor accept a library which failed that first check.
    mmd::ValidationOnce rejected, accepted;
    std::atomic<int> failedReads{0}, goodReads{0}, rejectedCalls{0};
    std::vector<std::thread> threads;
    for (int t=0; t<16; ++t) threads.emplace_back([&] {
        for (int i=0; i<1000; ++i) {
            try {
                rejected.check([&] { ++failedReads; throw std::runtime_error("unsupported engine fixture"); });
            } catch (const std::runtime_error& e) {
                if (std::string(e.what()) == "unsupported engine fixture") ++rejectedCalls;
            }
            accepted.check([&] { ++goodReads; });
        }
    });
    for (auto& t:threads) t.join();
    if (failedReads!=1 || goodReads!=1 || rejectedCalls!=16000) return 1;
    std::cout << "PASS: one verification per library, including cached failures across render passes\n";
}
