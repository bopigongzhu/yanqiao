#include "frequency.hpp"
#include <wincrypt.h>
#include <fstream>
#include <iostream>
using namespace es;
int main() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
        ++checks;
    };
    wchar_t temporary[MAX_PATH];
    GetTempPathW(MAX_PATH, temporary);
    auto path =
        std::filesystem::path(temporary) / (L"EShuRu-storage-" + std::to_wstring(GetCurrentProcessId()) +
                                            L"-" + std::to_wstring(now_ms()) + L".dat");
    auto file = [&](const wchar_t *suffix) {
        auto p = path;
        p += suffix;
        return p;
    };
    auto write = [&](const std::filesystem::path &p, const std::string &bytes) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << bytes;
        check(bool(f), "test fixture written");
    };
    auto bytes = [&](const std::filesystem::path &p) {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    auto seal = [&](const Json &j) {
        auto plain = j.dump();
        DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(plain.data())}, out{};
        check(CryptProtectData(&in, L"test", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out),
              "invalid-schema fixture encrypted by actual DPAPI");
        std::string encrypted(reinterpret_cast<char *>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return encrypted;
    };
    auto cleanup = [&] {
        for (auto suffix : {L"", L".bak", L".tmp", L".bak.tmp"})
            DeleteFileW(file(suffix).c_str());
    };
    int result = 0;
    try {
        const LearningSpan name{0, 2, {utf8(L"名字"), "ming zi"}, 3};
        {
            FrequencyStore store(path);
            check(!store.failed() && !store.take_recovery_notice(), "new empty store is healthy");
            store.learn({name});
            store.learn({name});
            check(store.flush(), "primary and recovery snapshot saved");
        }
        auto valid = bytes(path);
        check(!valid.empty() && !bytes(file(L".bak")).empty(), "both completed snapshots exist");
        for (auto suffix : {L"", L".bak"}) {
            auto stored = bytes(file(suffix));
            check(stored.find(name.word.text) == std::string::npos &&
                      stored.find("ming zi") == std::string::npos,
                  "neither primary nor backup contains plaintext words");
        }
        write(path, "interrupted primary");
        write(file(L".tmp"), seal(Json{{"v", 1}, {"words", Json::array()}}));
        {
            FrequencyStore recovered(path);
            check(!recovered.failed() && recovered.size() == 1 && !recovered.matching("mz").empty(),
                  "corrupt primary recovers committed backup, ignores temporary snapshot");
            check(recovered.take_recovery_notice() && !recovered.take_recovery_notice(),
                  "recovery notification delivered once");
            check(recovered.flush(), "recovery repairs primary in background");
        }
        {
            FrequencyStore repaired(path);
            check(repaired.size() == 1 && !repaired.take_recovery_notice(),
                  "repaired primary loads normally");
        }
        DeleteFileW(path.c_str());
        {
            FrequencyStore recovered(path);
            check(recovered.size() == 1 && recovered.take_recovery_notice() && recovered.flush(),
                  "missing primary recovers valid backup");
        }
        write(file(L".bak"), "bad backup");
        {
            FrequencyStore primary(path);
            check(primary.size() == 1 && !primary.failed() && !primary.take_recovery_notice(),
                  "valid primary takes precedence over corrupt backup");
        }
        write(file(L".bak"), valid);
        write(path, seal(Json{{"v", 1},
                              {"words", Json::array({Json::array({"partial", "ta", 6, 0}),
                                                     Json::array({"invalid", "ta", -1, 0})})}}));
        {
            FrequencyStore recovered(path);
            check(recovered.take_recovery_notice() && recovered.size() == 1 &&
                      recovered.matching("ta").empty() && recovered.flush(),
                  "invalid decrypted schema cannot partially populate memory before recovery");
        }
        write(path, "bad primary");
        write(file(L".bak"), "bad backup");
        write(file(L".bak.tmp"), valid);
        {
            FrequencyStore broken(path);
            check(broken.failed() && broken.size() == 0 && !broken.take_recovery_notice(),
                  "both corrupt snapshots use empty table, never unfinished backup temp");
            check(broken.clear() && !broken.failed(), "corrupt store can be cleared");
        }
        for (auto suffix : {L".bak", L".tmp", L".bak.tmp"})
            check(!std::filesystem::exists(file(suffix)), "clear removes recovery and temporary data");
        {
            FrequencyStore empty(path);
            check(empty.size() == 0 && !empty.failed(), "empty primary survives restart");
        }
        write(path, "damage after successful clear");
        {
            FrequencyStore empty(path);
            check(empty.size() == 0 && empty.failed(), "old words cannot resurrect after clear");
            check(empty.clear(), "restore empty primary");
            empty.learn({name});
            empty.learn({name});
            check(empty.flush(), "fixture populated for partial-write failure");
        }
        {
            FrequencyStore blocked(path);
            // A real Windows sharing violation forces backup replacement/deletion to fail.
            Handle held(CreateFileW(file(L".bak").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, 0, nullptr));
            check(bool(held), "owned backup locked against replacement");
            blocked.learn({name});
            check(!blocked.flush() && blocked.failed(), "partial save failure is reported");
            check(!blocked.clear() && blocked.failed(), "failed disk cleanup never reports clear success");
            held.reset();
            check(blocked.clear() && blocked.flush() && !blocked.failed(),
                  "cleanup can be retried after unlock");
        }
        std::vector<LearningSpan> many;
        for (int i = 0; i < 2000; ++i)
            many.push_back({0, 1, {"race-" + std::to_string(i), "ming zi"}, 3});
        {
            FrequencyStore racing(path);
            for (int i = 0; i < 16; ++i) {
                racing.learn(many);
                Sleep(i % 4 == 0 ? 0 : i % 4 == 1 ? 290 : i % 4 == 2 ? 300 : 310);
                check(racing.clear() && racing.flush(), "clear serialized with pending or active writer");
                FrequencyStore restart(path);
                check(restart.size() == 0 && !restart.take_recovery_notice(),
                      "restart after racing clear never restores pre-clear words");
                check(!std::filesystem::exists(file(L".bak")),
                      "obsolete writer cannot recreate backup after clear");
            }
            racing.learn(many);
            check(racing.clear(), "clear supersedes queued old snapshot");
            racing.learn({name});
            racing.learn({name});
            check(racing.flush(), "learning after clear can save normally");
        }
        {
            FrequencyStore fresh(path);
            check(fresh.size() == 1 && !fresh.matching("mz").empty(), "only post-clear learning survives");
            check(fresh.clear(), "final cleanup persists empty table");
        }
        std::cout << Json{{"checks", checks},
                          {"result", "passed"},
                          {"scope",
                           "Actual DPAPI, corrupted/missing snapshots, interrupted writes, Windows "
                           "sharing failures, cleanup and concurrent background saves; own files only."}}
                         .dump()
                  << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << " after " << checks << " checks\n";
        result = 1;
    }
    cleanup();
    return result;
}
