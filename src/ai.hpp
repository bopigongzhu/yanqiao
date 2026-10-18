#pragma once
#include "core.hpp"
#include <winhttp.h>
namespace es {
struct AiRequest {
    std::string session;
    uint64_t revision = 0;
    std::string source, translation;
    bool explanation = false;
    uint64_t request_id = 0;
};
class AiWorker {
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::optional<AiRequest> pending_;
    std::atomic_uint64_t generation_{0};
    HWND target_;
    HINTERNET session_ = nullptr;
    void run();
    void request(const AiRequest &, uint64_t generation);

  public:
    explicit AiWorker(HWND target);
    ~AiWorker();
    void submit(AiRequest request);
    void cancel();
};
} // namespace es
