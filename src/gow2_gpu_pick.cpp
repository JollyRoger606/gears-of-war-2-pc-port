// gow2 - ReXGlue Recompiled Project
#include "gow2_gpu_pick.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(gow2_gpu, "auto", "UI/D3D12",
                      "GPU to render on: auto (the one Windows sets as high performance), part "
                      "of its name (for example 7800) or its adapter index. Takes effect on the "
                      "next start");

#ifdef _WIN32

#include <algorithm>
#include <cctype>
#include <vector>

#include <windows.h>
#include <dxgi1_6.h>

namespace rex::glue {

namespace {

struct Adapter {
  int index;
  std::string name;
  LUID luid;
  SIZE_T dedicated;
};

std::string Narrow(const wchar_t* text) {
  char out[256]{};
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out, sizeof(out) - 1, nullptr, nullptr);
  return out;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

IDXGIFactory1* CreateFactory() {
  using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
  HMODULE dxgi = LoadLibraryA("dxgi.dll");
  auto create =
      dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
  IDXGIFactory1* factory = nullptr;
  if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    return nullptr;
  }
  return factory;
}

std::vector<Adapter> ListHardwareAdapters(IDXGIFactory1* factory) {
  std::vector<Adapter> adapters;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      adapters.push_back({int(i), Narrow(desc.Description), desc.AdapterLuid,
                          desc.DedicatedVideoMemory});
    }
    adapter->Release();
  }
  return adapters;
}

// Windows' own ranking: the per-app choice in Graphics settings, then the system default
// high-performance GPU, then discrete over integrated. Shared memory reserved for an
// integrated GPU counts as dedicated memory in DXGI, so ranking by memory picks it.
bool HighPerformanceLuid(IDXGIFactory1* factory, LUID* out) {
  IDXGIFactory6* factory6 = nullptr;
  if (FAILED(factory->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory6)))) {
    return false;
  }
  bool found = false;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; !found && SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
                                 reinterpret_cast<void**>(&adapter)));
       ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      *out = desc.AdapterLuid;
      found = true;
    }
    adapter->Release();
  }
  factory6->Release();
  return found;
}

const Adapter* FindByLuid(const std::vector<Adapter>& adapters, LUID luid) {
  for (const Adapter& a : adapters) {
    if (a.luid.LowPart == luid.LowPart && a.luid.HighPart == luid.HighPart) return &a;
  }
  return nullptr;
}

const Adapter* FindByIndexText(const std::vector<Adapter>& adapters, const std::string& wanted) {
  if (wanted.empty() || !std::all_of(wanted.begin(), wanted.end(), ::isdigit)) return nullptr;
  const int index = std::atoi(wanted.c_str());
  for (const Adapter& a : adapters) {
    if (a.index == index) return &a;
  }
  return nullptr;
}

const Adapter* FindByName(const std::vector<Adapter>& adapters, const std::string& wanted) {
  const std::string needle = Lower(wanted);
  for (const Adapter& a : adapters) {
    if (Lower(a.name).find(needle) != std::string::npos) return &a;
  }
  return nullptr;
}

const Adapter* LargestMemory(const std::vector<Adapter>& adapters) {
  const Adapter* best = nullptr;
  for (const Adapter& a : adapters) {
    if (!best || a.dedicated > best->dedicated) best = &a;
  }
  return best;
}

GpuChoice ToChoice(const Adapter* adapter, const char* reason) {
  if (!adapter) return {};
  return {adapter->index, adapter->name, reason};
}

GpuChoice ChooseAuto(IDXGIFactory1* factory, const std::vector<Adapter>& adapters) {
  LUID luid{};
  if (HighPerformanceLuid(factory, &luid)) {
    if (const Adapter* a = FindByLuid(adapters, luid)) return ToChoice(a, "Windows high-performance GPU");
  }
  return ToChoice(LargestMemory(adapters), "largest dedicated memory");
}

GpuChoice ChooseFromSetting(IDXGIFactory1* factory, const std::vector<Adapter>& adapters,
                            const std::string& setting) {
  if (setting.empty() || Lower(setting) == "auto") return ChooseAuto(factory, adapters);
  if (const Adapter* a = FindByIndexText(adapters, setting)) return ToChoice(a, "gow2_gpu index");
  if (const Adapter* a = FindByName(adapters, setting)) return ToChoice(a, "gow2_gpu name");
  GpuChoice fallback = ChooseAuto(factory, adapters);
  fallback.reason = "no adapter matches gow2_gpu '" + setting + "', " + fallback.reason;
  return fallback;
}

}  // namespace

GpuChoice ChooseRenderGpu() {
  IDXGIFactory1* factory = CreateFactory();
  if (!factory) return {};
  const std::vector<Adapter> adapters = ListHardwareAdapters(factory);
  const GpuChoice choice = ChooseFromSetting(factory, adapters, REXCVAR_GET(gow2_gpu));
  factory->Release();
  return choice;
}

}  // namespace rex::glue

#else

namespace rex::glue {

GpuChoice ChooseRenderGpu() { return {}; }

}  // namespace rex::glue

#endif
