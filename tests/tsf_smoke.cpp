#include "common.hpp"
#include <msctf.h>
#include <iostream>

using namespace es;
struct ProcessorEx : ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *, TfClientId, DWORD) = 0;
};
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code = 0;
    HMODULE dll = LoadLibraryExW((module_dir() / L"EShuRuTip.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    try {
        auto require = [](bool condition, const char *name) {
            if (!condition)
                throw std::runtime_error(name);
        };
        require(dll != nullptr, "load native TSF DLL");
        using FactoryFn = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        auto create = reinterpret_cast<FactoryFn>(GetProcAddress(dll, "DllGetClassObject"));
        CLSID clsid{0x5db324ae, 0x9ed8, 0x4e80, {0x9b, 0xce, 0x85, 0x20, 0xdf, 0xfa, 0x3c, 0x93}};
        GUID iid{0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
        {
            ComPtr<IClassFactory> factory;
            require(create &&
                        SUCCEEDED(create(clsid, IID_IClassFactory, reinterpret_cast<void **>(factory.put()))),
                    "factory");
            ComPtr<ProcessorEx> service;
            require(
                SUCCEEDED(factory->CreateInstance(nullptr, iid, reinterpret_cast<void **>(service.put()))),
                "extended processor interface");
            ComPtr<ITfThreadMgr> manager;
            require(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_ITfThreadMgr, reinterpret_cast<void **>(manager.put()))),
                    "real Windows TSF thread manager");
            TfClientId client = 0;
            ComPtr<ITfThreadMgrEx> manager_ex;
            require(SUCCEEDED(manager->QueryInterface(IID_ITfThreadMgrEx,
                                                      reinterpret_cast<void **>(manager_ex.put()))),
                    "extended TSF manager");
            require(SUCCEEDED(manager_ex->ActivateEx(&client, TF_TMAE_SECUREMODE)),
                    "activate secure TSF manager");
            require(SUCCEEDED(service->ActivateEx(manager.get(), client, TF_TMAE_SECUREMODE)),
                    "activate service in secure mode");
            ComPtr<ITfKeyEventSink> keys;
            GUID layout_iid{0x2af2d06a, 0xdd5b, 0x4927, {0xa0, 0xb4, 0x54, 0xf1, 0x9c, 0x91, 0xfa, 0xde}};
            ComPtr<IUnknown> layout;
            require(SUCCEEDED(service->QueryInterface(layout_iid, reinterpret_cast<void **>(layout.put()))),
                    "layout event sink exposed");
            require(SUCCEEDED(
                        service->QueryInterface(IID_ITfKeyEventSink, reinterpret_cast<void **>(keys.put()))),
                    "key event sink");
            BOOL eaten = TRUE;
            keys->OnTestKeyDown(nullptr, 'A', 0, &eaten);
            require(!eaten, "secure mode never intercepts input");
            require(SUCCEEDED(service->Deactivate()), "deactivate service");
            manager->Deactivate();
        }
        using UnloadFn = HRESULT(WINAPI *)();
        auto unload = reinterpret_cast<UnloadFn>(GetProcAddress(dll, "DllCanUnloadNow"));
        require(unload && unload() == S_OK, "no outstanding COM objects");
        std::cout << Json{{"pointer_bits", sizeof(void *) * 8},
                          {"secure_activation", true},
                          {"unloads", true},
                          {"scope", "Real Windows TSF activation in isolated secure test thread; no input "
                                    "profile registration"}}
                         .dump()
                  << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        code = 1;
    }
    if (dll)
        FreeLibrary(dll);
    CoUninitialize();
    return code;
}
