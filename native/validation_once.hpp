#pragma once
#include <exception>
#include <mutex>

namespace mmd {
// std::call_once normally retries when its callback throws. A rejected engine
// build cannot repair itself mid-session: retain that result, including errors,
// so rendering cannot turn a DLL fingerprint check into per-frame disk I/O.
class ValidationOnce {
    std::once_flag once;
    std::exception_ptr error;
public:
    template<class F> void check(F&& validate) {
        std::call_once(once, [&] {
            try { validate(); } catch (...) { error = std::current_exception(); }
        });
        if (error) std::rethrow_exception(error);
    }
};
}
