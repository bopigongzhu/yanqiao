#include "ipc.hpp"
#include <sddl.h>
namespace es {
static bool transfer(HANDLE pipe, HANDLE stop, void *buffer, DWORD size, bool writing) {
    auto p = static_cast<BYTE *>(buffer);
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event)
        return false;
    while (size) {
        if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT)
            return false;
        OVERLAPPED ov{};
        ov.hEvent = event.value;
        ResetEvent(event.value);
        DWORD count = 0;
        BOOL ok = writing ? WriteFile(pipe, p, size, &count, &ov) : ReadFile(pipe, p, size, &count, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING)
            return false;
        HANDLE events[]{event.value, stop};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &ov);
            GetOverlappedResult(pipe, &ov, &count, TRUE);
            return false;
        }
        if (!GetOverlappedResult(pipe, &ov, &count, TRUE) || !count)
            return false;
        p += count;
        size -= count;
    }
    return true;
}
static bool read_exact(HANDLE p, HANDLE stop, void *b, DWORD n) {
    return transfer(p, stop, b, n, false);
}
static bool write_exact(HANDLE p, HANDLE stop, const void *b, DWORD n) {
    return transfer(p, stop, const_cast<void *>(b), n, true);
}
Channel::~Channel() {
    stop();
}
void Channel::start(std::function<void(std::shared_ptr<Channel>, Json)> receive) {
    receive_ = std::move(receive);
    alive = true;
    auto self = shared_from_this();
    reader_ = std::thread([this, self] {
        while (alive) {
            DWORD size = 0;
            if (!read_exact(pipe_, stop_signal_.value, &size, 4) || !size || size > MAX_FRAME)
                break;
            std::string text(size, '\0');
            if (!read_exact(pipe_, stop_signal_.value, text.data(), size))
                break;
            try {
                Json message = Json::parse(text);
                if (!message.is_object() || message.value("v", 0) != 1)
                    break;
                receive_(self, std::move(message));
            } catch (...) {
                break;
            }
        }
        cancel();
        receive_(self, Json{{"v", 1}, {"type", "disconnected"}});
    });
    writer_ = std::thread([this] {
        while (alive) {
            std::string text;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return !alive || !outgoing_.empty(); });
                if (!alive)
                    break;
                text = std::move(outgoing_.front());
                outgoing_.pop_front();
            }
            DWORD size = DWORD(text.size());
            if (!write_exact(pipe_, stop_signal_.value, &size, 4) ||
                !write_exact(pipe_, stop_signal_.value, text.data(), size)) {
                cancel();
                break;
            }
        }
    });
}
bool Channel::send(const Json &message) {
    try {
        Json copy = message;
        copy["v"] = 1;
        auto text = copy.dump();
        if (text.size() > MAX_FRAME || !alive)
            return false;
        {
            std::lock_guard lock(mutex_);
            if (outgoing_.size() >= 128) {
                cancel();
                return false;
            }
            outgoing_.push_back(std::move(text));
        }
        cv_.notify_one();
        return true;
    } catch (...) {
        return false;
    }
}
void Channel::cancel() {
    alive = false;
    SetEvent(stop_signal_.value);
    cv_.notify_all();
    if (pipe_ != INVALID_HANDLE_VALUE)
        CancelIoEx(pipe_, nullptr);
}
void Channel::stop() {
    cancel();
    if (reader_.joinable()) {
        CancelSynchronousIo(reader_.native_handle());
        reader_.join();
    }
    if (writer_.joinable()) {
        CancelSynchronousIo(writer_.native_handle());
        writer_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    std::lock_guard lock(mutex_);
    outgoing_.clear();
}
void PipeServer::start(HWND target) {
    target_ = target;
    listener_ = std::thread([this] {
        auto name = pipe_name();
        std::wstring sddl = L"D:P(A;;GA;;;" + user_sid() + L")";
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd,
                                                                  nullptr))
            return;
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        while (!stop_) {
            HANDLE pipe =
                CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 16, 65536, 65536, 0, &sa);
            if (pipe == INVALID_HANDLE_VALUE)
                break;
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED ov{};
            ov.hEvent = event.value;
            BOOL initial = ConnectNamedPipe(pipe, &ov);
            DWORD error = GetLastError();
            bool connected = initial || error == ERROR_PIPE_CONNECTED;
            if (!connected && error == ERROR_IO_PENDING) {
                HANDLE events[]{event.value, stop_signal_.value};
                WaitForMultipleObjects(2, events, FALSE, INFINITE);
                DWORD n = 0;
                if (stop_)
                    CancelIoEx(pipe, &ov);
                connected = GetOverlappedResult(pipe, &ov, &n, TRUE) != FALSE;
            }
            if (stop_ || !connected) {
                CloseHandle(pipe);
                if (stop_)
                    break;
                continue;
            }
            auto peer = std::make_shared<Channel>(pipe);
            ULONG pid = 0;
            if (!GetNamedPipeClientProcessId(pipe, &pid)) {
                continue;
            }
            peer->peer_pid = pid;
            {
                std::lock_guard lock(mutex_);
                for (auto it = clients_.begin(); it != clients_.end();) {
                    if (!(*it)->alive) {
                        (*it)->stop();
                        it = clients_.erase(it);
                    } else
                        ++it;
                }
                clients_.push_back(peer);
            }
            peer->start([this](auto channel, Json j) {
                if (queued_->fetch_add(1) >= 128) {
                    --*queued_;
                    channel->cancel();
                    return;
                }
                auto e = new PipeEvent{std::move(channel), std::move(j), queued_};
                if (!PostMessageW(target_, WM_ES_EVENT, 0, reinterpret_cast<LPARAM>(e)))
                    delete e;
            });
        }
        LocalFree(sd);
    });
}
void PipeServer::stop() {
    if (stop_.exchange(true))
        return;
    SetEvent(stop_signal_.value);
    if (listener_.joinable()) {
        CancelSynchronousIo(listener_.native_handle());
        listener_.join();
    }
    std::lock_guard lock(mutex_);
    for (auto &c : clients_)
        c->stop();
    clients_.clear();
}
void PipeClient::start(const std::filesystem::path &root, HWND target) {
    stop();
    stop_ = false;
    ResetEvent(stop_signal_.value);
    target_ = target;
    connector_ = std::thread([this, root] {
        uint64_t next_launch = 0;
        bool notified_pause = false;
        while (!stop_) {
            if (!auto_start_allowed()) {
                if (!notified_pause) {
                    auto e = new PipeEvent{nullptr, Json{{"type", "paused"}}};
                    if (!PostMessageW(target_, WM_ES_EVENT, 0, reinterpret_cast<LPARAM>(e)))
                        delete e;
                    notified_pause = true;
                }
                WaitForSingleObject(stop_signal_.value, 1000);
                continue;
            }
            notified_pause = false;
            HANDLE pipe = CreateFileW(pipe_name().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                if (now_ms() >= next_launch) {
                    launch_companion(root);
                    next_launch = now_ms() + 5000;
                }
                WaitForSingleObject(stop_signal_.value, 250);
                continue;
            }
            ULONG pid = 0;
            bool verified = false;
            if (GetNamedPipeServerProcessId(pipe, &pid)) {
                Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
                wchar_t image[32768];
                DWORD n = 32768;
                if (process && QueryFullProcessImageNameW(process.value, 0, image, &n))
                    verified = _wcsicmp(image, (root / L"EShuRu.exe").c_str()) == 0;
            }
            if (!verified) {
                CloseHandle(pipe);
                // Another build may own this user's pipe. Never trust or kill it.
                WaitForSingleObject(stop_signal_.value, 1000);
                continue;
            }
            auto channel = std::make_shared<Channel>(pipe);
            channel->peer_pid = pid;
            {
                std::lock_guard lock(mutex_);
                channel_ = channel;
            }
            channel->start([this](auto c, Json j) {
                auto e = new PipeEvent{std::move(c), std::move(j)};
                if (!PostMessageW(target_, WM_ES_EVENT, 0, reinterpret_cast<LPARAM>(e)))
                    delete e;
            });
            channel->send(Json{{"type", "hello"}, {"pid", GetCurrentProcessId()}});
            HANDLE events[]{stop_signal_.value, channel->closed_event()};
            WaitForMultipleObjects(2, events, FALSE, INFINITE);
            channel->stop(); // joins callbacks before a replacement connection can post events
            {
                std::lock_guard lock(mutex_);
                channel_.reset();
            }
            WaitForSingleObject(stop_signal_.value, 250);
        }
    });
}
void PipeClient::stop() {
    stop_ = true;
    SetEvent(stop_signal_.value);
    if (connector_.joinable())
        connector_.join();
    std::shared_ptr<Channel> c;
    {
        std::lock_guard lock(mutex_);
        c = channel_;
        channel_.reset();
    }
    if (c)
        c->stop();
}
bool PipeClient::send(const Json &j) {
    std::lock_guard lock(mutex_);
    return channel_ && channel_->send(j);
}
bool PipeClient::ready() {
    std::lock_guard lock(mutex_);
    return channel_ && channel_->alive;
}
bool PipeClient::current(const std::shared_ptr<Channel> &peer) {
    std::lock_guard lock(mutex_);
    return peer && peer == channel_ && peer->alive;
}
} // namespace es
