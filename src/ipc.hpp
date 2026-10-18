#pragma once
#include "common.hpp"
namespace es {
class Channel : public std::enable_shared_from_this<Channel> {
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    Handle stop_signal_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread reader_, writer_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> outgoing_;
    std::function<void(std::shared_ptr<Channel>, Json)> receive_;

  public:
    std::atomic_bool alive{false};
    DWORD peer_pid = 0;
    explicit Channel(HANDLE pipe) : pipe_(pipe) {}
    ~Channel();
    void start(std::function<void(std::shared_ptr<Channel>, Json)> receive);
    bool send(const Json &message);
    void cancel();
    void stop();
    HANDLE closed_event() const {
        return stop_signal_.value;
    }
};
struct PipeEvent {
    std::shared_ptr<Channel> peer;
    Json message;
    std::shared_ptr<std::atomic_uint> queued;
    ~PipeEvent() {
        if (queued)
            --*queued;
    }
};
class PipeServer {
    std::thread listener_;
    std::atomic_bool stop_{false};
    std::mutex mutex_;
    std::vector<std::shared_ptr<Channel>> clients_;
    HWND target_ = nullptr;
    Handle stop_signal_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::shared_ptr<std::atomic_uint> queued_ = std::make_shared<std::atomic_uint>(0);

  public:
    ~PipeServer() {
        stop();
    }
    void start(HWND target);
    void stop();
};
class PipeClient {
    Handle stop_signal_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread connector_;
    std::atomic_bool stop_{false};
    std::mutex mutex_;
    std::shared_ptr<Channel> channel_;
    HWND target_ = nullptr;

  public:
    ~PipeClient() {
        stop();
    }
    void start(const std::filesystem::path &root, HWND target);
    void stop();
    bool send(const Json &message);
    bool ready();
    bool current(const std::shared_ptr<Channel> &peer);
};
} // namespace es
