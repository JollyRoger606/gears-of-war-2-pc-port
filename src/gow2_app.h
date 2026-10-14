// gow2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#endif

class Gow2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Gow2App>(new Gow2App(ctx, "gow2",
        PPCImageConfig));
  }

  static std::filesystem::path WithXex(const std::filesystem::path& dir) {
    std::error_code ec;
    if (dir.empty()) return {};
    if (std::filesystem::exists(dir / "default.xex", ec)) return dir;
    for (std::filesystem::directory_iterator it(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
      std::error_code e2;
      if (!it->is_directory(e2)) continue;
      if (std::filesystem::exists(it->path() / "default.xex", e2)) return it->path();
    }
    return {};
  }

  static std::filesystem::path AppHome() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"GOW2_HOME", buf, MAX_PATH * 2);
    if (n && n < MAX_PATH * 2) return std::filesystem::path(buf);
#endif
    return rex::filesystem::GetExecutablePath().parent_path();
  }

  static std::filesystem::path ResolveContentRoot(const std::filesystem::path& given) {
    std::error_code ec;
    if (!given.empty() && std::filesystem::exists(given, ec)) {
      if (auto hit = WithXex(given); !hit.empty()) return hit;
    }
    std::filesystem::path dir = AppHome();
    for (int depth = 0; !dir.empty(); ++depth) {
      if (auto hit = WithXex(dir); !hit.empty()) {
        if (hit != given) REXLOG_INFO("gow2 found content root {}", hit.string());
        return hit;
      }
      if (depth >= 4) break;
      auto parent = dir.parent_path();
      if (parent == dir) break;
      dir = parent;
    }
    return given;
  }

  static bool SetIfUnset(const char* name, const std::string& value) {
    if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) {
      return false;
    }
    return rex::cvar::SetFlagByName(name, value);
  }

  // With d3d12_adapter=-1 the runtime picked the integrated AMD Radeon Graphics instead of the RX 7800 XT on the dev PC.
  static void PickFastestAdapter() {
#ifdef _WIN32
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    auto create = dxgi ? reinterpret_cast<CreateFactoryFn>(
                             GetProcAddress(dxgi, "CreateDXGIFactory1"))
                       : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1),
                                 reinterpret_cast<void**>(&factory)))) {
      return;
    }
    int best = -1;
    SIZE_T best_vram = 0;
    std::string best_name;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc{};
      if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
          !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
          desc.DedicatedVideoMemory > best_vram) {
        best = static_cast<int>(i);
        best_vram = desc.DedicatedVideoMemory;
        char name[128]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1,
                            nullptr, nullptr);
        best_name = name;
      }
      adapter->Release();
    }
    factory->Release();
    if (best >= 0 && SetIfUnset("d3d12_adapter", std::to_string(best))) {
      REXLOG_INFO("gow2 d3d12_adapter -> {} ({}, {} MB VRAM)", best, best_name,
                  best_vram >> 20);
    }
#endif
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    PickFastestAdapter();
    config.gpu_plugin = "xenos";
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // Find the disc folder next to the exe so a double-click boots without --game_data_root.
    paths.game_data_root = ResolveContentRoot(paths.game_data_root);
    paths.update_data_root = paths.game_data_root / "$SystemUpdate";
#ifdef _WIN32
    if (wchar_t local[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
      const std::filesystem::path base = std::filesystem::path(local) / L"GearsOfWar2PC";
      paths.cache_root = base / "cache";
      paths.user_data_root = base / "user";
      std::error_code ec;
      std::filesystem::create_directories(paths.cache_root, ec);
      std::filesystem::create_directories(paths.user_data_root, ec);
    }
#endif
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
};
