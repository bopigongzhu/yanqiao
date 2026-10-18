#include "ipc.hpp"
#include <iostream>
using namespace es;
int main() {
    try {
        for (int i = 0; i < 32; ++i) {
            auto name = L"\\\\.\\pipe\\EShuRu.CancelTest." + std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(i);
            HANDLE server =
                CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 1, 64, 64, 0, nullptr);
            if (server == INVALID_HANDLE_VALUE)
                throw std::runtime_error("create test pipe");
            Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OVERLAPPED, nullptr));
            if (!client) {
                CloseHandle(server);
                throw std::runtime_error("connect test pipe");
            }
            auto channel = std::make_shared<Channel>(server);
            channel->start([](auto, Json) {});
            // The other endpoint never reads or sends: both directions can be pending.
            channel->send(Json{{"type", "test"}, {"data", std::string(32000, 'x')}});
            if (i % 2)
                Sleep(1);
            auto start = Clock::now();
            channel->stop();
            if (Clock::now() - start > std::chrono::seconds(2))
                throw std::runtime_error("pipe shutdown blocked");
        }
        std::cout << "32 stalled-peer cancellation cycles passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
