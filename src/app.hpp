#pragma once
#include "panel.hpp"
#include "ipc.hpp"
#include "rime_engine.hpp"
#include "ai.hpp"
namespace es {
class App {
    HWND hwnd_ = nullptr;
    Panel panel_;
    PipeServer server_;
    RimeEngine rime_;
    std::unique_ptr<FrequencyStore> frequency_;
    bool learning_enabled_ = false;
    void configure_learning();
    InputBuffer buffer_;
    std::unique_ptr<AiWorker> translation_, explanation_;
    std::shared_ptr<Channel> peer_;
    std::weak_ptr<Channel> interrupted_peer_;
    std::string session_;
    uint64_t sequence_ = 0;
    uint64_t layout_sequence_ = 0;
    bool anchor_visible_ = true;
    uint64_t explanation_request_ = 0;
    RECT anchor_{300, 220, 300, 242};
    Json settings_;
    bool configured_ = false, headless_ = false, loading_ = false, blocked_ = false;
    uint64_t last_start_ = 0;
    std::optional<AiRequest> pending_;
    std::wstring error_, status_;
    void event(PipeEvent &);
    void key(const Json &);
    void source_changed(uint64_t before);
    void schedule();
    void action(int, int = 0);
    void refresh();
    void end();
    void result(AiResult &);
    void reply_state();
    void commit(bool english);
    void request_explanation();
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);

  public:
    int run(bool headless, bool settings);
    int render(const std::filesystem::path &path, int scenario = 0);
};
} // namespace es
