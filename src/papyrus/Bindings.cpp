#include "PCH.h"
#include "Bindings.h"
#include "capture/CaptureSession.h"
#include "capture/CaptureRequest.h"
#include "ui/MenuEventSink.h"
#include "ui/UIController.h"
#include "ConsoleCommandQueue.h"
#include "stringutils.h"

// ============================================================
// Script name (must match the Papyrus .psc file)
// ============================================================
static constexpr auto kScriptName = "Printscreen_Formula_script";

// ============================================================
// Helpers
// ============================================================
namespace {

// Queue a SKSE mod-callback event to notify Papyrus asynchronously.
// Payload is a JSON string with status, message, and optional path fields.
static void QueueModEvent(const std::string& eventName,
                          const std::string& status,
                          const std::string& message,
                          const std::string& outputPath = "") {
    auto* task = SKSE::GetTaskInterface();
    if (!task) return;

    // Build JSON payload (simple concatenation, no external dependency)
    std::string json = "{\"status\":\"" + status + "\"," +
                       "\"message\":\"" + message + "\"";
    if (!outputPath.empty()) {
        json += ",\"path\":\"" + outputPath + "\"";
    }
    json += "}";

    task->AddTask([eventName, json]() {
        auto* src = SKSE::GetModCallbackEventSource();
        if (!src) return;
        SKSE::ModCallbackEvent ev{
            RE::BSFixedString(eventName.c_str()),
            RE::BSFixedString(json.c_str()),
            0.0f, nullptr
        };
        src->SendEvent(&ev);
    });
}

// Legacy payload-only overload for backward compat
static void QueueModEvent(const std::string& payload) {
    QueueModEvent("PrintScreenComplete", "unknown", payload);
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// Build a CaptureRequest from the flat Papyrus arguments.
static CaptureRequest BuildRequest(
    const std::string& basePath,
    const std::string& imageType,
    float jpgQuality,
    const std::string& compressionMode,
    float duration,
    float fps,
    int   loopCount,
    int   deltaMode,
    int   optimize,
    float videoDuration,
    int   targetResolution,
    int   videoFrameRate,
    int   qualityPreset,
    int   videoBitrateKbps,
    float keyframeIntervalSec,
    int   encoderPreference,
    int   rateControl,
    int   videoContainer)
{
    CaptureRequest req;
    req.outputDir        = SKSE::stl::utf8_to_utf16(basePath).value_or(L"");
    req.format           = ParseImageFormat(ToLower(imageType));
    req.jpegQuality      = std::clamp(jpgQuality, 0.0f, 100.0f);
    req.ddsMode          = ParseDDSMode(compressionMode);
    req.animDuration     = duration;
    req.animFPS          = fps;
    req.loopCount        = loopCount;
    req.deltaMode        = deltaMode;
    req.optimize         = optimize;
    req.videoDuration    = videoDuration;
    req.targetResolution = std::clamp(targetResolution, 0, 4);
    req.videoFrameRate   = std::clamp(videoFrameRate, 30, 60);
    req.qualityPreset    = std::clamp(qualityPreset, 0, 4);
    req.videoBitrateKbps = std::clamp(videoBitrateKbps, 1000, 100000);
    req.keyframeIntervalSec = std::clamp(keyframeIntervalSec, 0.5f, 10.0f);
    req.encoderPreference   = std::clamp(encoderPreference, 0, 4);
    req.rateControl         = std::clamp(rateControl, 0, 4);
    req.videoContainer      = std::clamp(videoContainer, 0, 0); // only MP4 for now
    return req;
}

} // namespace

// ---------------------------------------------------------------------------
// JSON-native bridge -- parse a JSON string into a CaptureRequest.
// Uses nlohmann/json (assumes vcpkg dependency is present).
// ---------------------------------------------------------------------------
#include <nlohmann/json.hpp>

static CaptureRequest ParseRequestJson(const std::string& jsonStr) {
    CaptureRequest req;
    try {
        auto j = nlohmann::json::parse(jsonStr);

        if (j.contains("basePath") && j["basePath"].is_string()) {
            req.outputDir = SKSE::stl::utf8_to_utf16(j["basePath"].get<std::string>()).value_or(L"");
        }
        if (j.contains("imageType") && j["imageType"].is_string()) {
            req.format = ParseImageFormat(j["imageType"].get<std::string>());
        }
        if (j.contains("jpgCompression")) {
            req.jpegQuality = std::clamp(j["jpgCompression"].get<float>(), 0.0f, 100.0f);
        }
        if (j.contains("mode") && j["mode"].is_string()) {
            req.ddsMode = ParseDDSMode(j["mode"].get<std::string>());
            req.tiffMode = ParseTiffMode(j["mode"].get<std::string>());
        }
        if (j.contains("duration")) {
            req.animDuration = std::clamp(j["duration"].get<float>(), 0.1f, 60.0f);
            req.videoDuration = std::clamp(j["duration"].get<float>(), 1.0f, 120.0f);
        }
        if (j.contains("fps")) {
            req.animFPS = std::clamp(j["fps"].get<float>(), 1.0f, 60.0f);
        }
        if (j.contains("loopCount")) {
            req.loopCount = std::clamp(j["loopCount"].get<int>(), 0, 100);
        }
        if (j.contains("optimize")) {
            req.optimize = j["optimize"].get<int>() != 0 ? 1 : 0;
        }
        if (j.contains("targetResolution")) {
            req.targetResolution = std::clamp(j["targetResolution"].get<int>(), 0, 4);
        }
        if (j.contains("videoFrameRate")) {
            req.videoFrameRate = (j["videoFrameRate"].get<int>() == 60) ? 60 : 30;
        }
        if (j.contains("qualityPreset")) {
            req.qualityPreset = std::clamp(j["qualityPreset"].get<int>(), 0, 4);
        }
        if (j.contains("videoBitrate")) {
            req.videoBitrateKbps = std::clamp(j["videoBitrate"].get<int>(), 1000, 50000);
        }
        if (j.contains("keyframeInterval")) {
            req.keyframeIntervalSec = std::clamp(j["keyframeInterval"].get<float>(), 0.5f, 10.0f);
        }
        if (j.contains("encoderPreference")) {
            req.encoderPreference = std::clamp(j["encoderPreference"].get<int>(), 0, 4);
        }
        if (j.contains("rateControl")) {
            req.rateControl = std::clamp(j["rateControl"].get<int>(), 0, 4);
        }
        if (j.contains("videoContainer")) {
            req.videoContainer = std::clamp(j["videoContainer"].get<int>(), 0, 0);
        }
        if (j.contains("autoUI") && j["autoUI"].is_boolean()) {
            req.autoUI = j["autoUI"].get<bool>();
        }
    } catch (const std::exception& e) {
        logger::error("ParseRequestJson failed: {}", e.what());
    }
    return req;
}

static std::string TakePhoto_Internal_Json(RE::StaticFunctionTag*,
                                          std::string jsonConfig) {
    CaptureRequest req = ParseRequestJson(jsonConfig);
    auto startResult = CaptureSession::GetSingleton().Start(req, [](const std::string& r) {
        QueueModEvent(r);
    });

    switch (startResult) {
        case CaptureSession::StartResult::Accepted:
            return "Starting";
        case CaptureSession::StartResult::BusyCancelled:
            return "Previous capture cancelled";
        default:
            return "Error: Unable to start capture";
    }
}
static bool IsGamePaused_Cached(RE::StaticFunctionTag*) {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    // Common pause menus
    static const std::set<std::string> pauseMenus = {
        "InventoryMenu", "MagicMenu", "Journal Menu", "MapMenu",
        "FavoritesMenu", "Book Menu", "BarterMenu", "ContainerMenu",
        "Dialogue Menu", "GiftMenu", "Lockpicking Menu", "Sleep/Wait Menu",
        "StatsMenu", "Training Menu", "Tutorial Menu"
    };
    for (const auto& name : pauseMenus) {
        if (ui->IsMenuOpen(name.c_str())) {
            auto menu = ui->GetMenu(name.c_str());
            if (menu) {
                auto* im = static_cast<RE::IMenu*>(menu.get());
                if (im && im->menuFlags.any(RE::UI_MENU_FLAGS::kPausesGame)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool IsTextInputActive() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    // Check for console or text input menus
    if (ui->IsMenuOpen("Console")) return true;
    if (ui->IsMenuOpen("Console Native UI Menu")) return true;
    return false;
}

// ============================================================
// UI visibility helpers (direct Scaleform — no console commands)
// ============================================================
namespace {

// Queue UI hide to game thread via SKSE task interface.
// Safe to call from any thread.
static void HideUIAsync() {
    auto* task = SKSE::GetTaskInterface();
    if (task) {
        task->AddTask([]() {
            UIController::GetSingleton().HideAll();
        });
    }
}

// Queue UI restore to game thread via SKSE task interface.
// Safe to call from any thread.
static void RestoreUIAsync() {
    auto* task = SKSE::GetTaskInterface();
    if (task) {
        task->AddTask([]() {
            UIController::GetSingleton().RestoreAll();
        });
    }
}

} // namespace

// ============================================================
// Papyrus-bound functions
// ============================================================

static std::string TakePhoto(
    RE::StaticFunctionTag*,
    std::string basePath, std::string imageType,
    float jpgQuality, std::string compressionMode,
    float duration, float fps,
    int loopCount, int deltaMode, int optimize,
    float videoDuration, int targetResolution, int videoFrameRate,
    int qualityPreset, int videoBitrateKbps,
    float keyframeIntervalSec, int encoderPreference,
    int rateControl, int videoContainer,
    bool autoUI)
{
    logger::info("Papyrus TakePhoto: type={}, autoUI={}", imageType, autoUI);
    auto req = BuildRequest(basePath, imageType, jpgQuality, compressionMode,
                            duration, fps, loopCount, deltaMode, optimize,
                            videoDuration, targetResolution, videoFrameRate,
                            qualityPreset, videoBitrateKbps,
                            keyframeIntervalSec, encoderPreference,
                            rateControl, videoContainer);
    req.autoUI = autoUI;

    auto& session = CaptureSession::GetSingleton();

    // Queue the UI hide before starting the worker thread. CaptureSession::Start()
    // holds its mutex until the thread is created, so the worker cannot call
    // SetResult (and queue UI restore) until after Start() returns. Queuing
    // HideUIAsync here guarantees [Hide, Restore] order in the SKSE task
    // queue even when a still capture completes before the calling frame ends.
    if (autoUI) {
        HideUIAsync();
        MenuEventSink::GetSingleton()->SetMenusHidden(true);
    }

    auto startResult = session.Start(
        req,
        // Completion callback — fires after encoding (or on error/cancel).
        // Sends structured JSON event to Papyrus with status, message, and path.
        // Executes on the worker thread; UI restore is marshalled to game thread.
        [autoUI, outputDir = req.outputDir](const std::string& r) {
            std::string status, message;
            if (r == "CALLBACK_SUCCESS") {
                status = "success";
                message = "Capture completed successfully";
            } else if (r == "CALLBACK_CANCELLED") {
                status = "cancelled";
                message = "Capture was cancelled";
            } else if (r.find("CALLBACK_ERROR:") == 0) {
                status = "error";
                message = r.substr(15);
            } else if (r == "Already running" || r.find("Error") == 0) {
                status = "error";
                message = r;
            } else {
                status = "unknown";
                message = r;
            }
            QueueModEvent("PrintScreenComplete", status, message,
                          util::wstring_to_utf8(outputDir));
            MenuEventSink::GetSingleton()->ClearCaptureToken();
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
        },
        // Acquisition callback — fires after all frames are captured, before encoding.
        // Restores HUD immediately so slow DDS/AGIF/APNG encodes don't suppress UI.
        // Executes on the worker thread; marshalled to game thread via task queue.
        autoUI ? CaptureSession::AcquisitionCallback([]() {
            RestoreUIAsync();
            MenuEventSink::GetSingleton()->SetMenusHidden(false);
        }) : CaptureSession::AcquisitionCallback{});

    switch (startResult) {
        case CaptureSession::StartResult::Accepted:
            break;
        case CaptureSession::StartResult::BusyCancelled:
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
            return "Previous capture cancelled";
        default:
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
            return "Error: Unable to start capture";
    }

    if (autoUI) {
        MenuEventSink::GetSingleton()->SetCaptureToken(session.GetToken());
    }
    return "Started";
}

static std::string GetResult(RE::StaticFunctionTag*) {
    // DEPRECATED: Polling is no longer supported. Use the PrintScreenComplete
    // mod event instead. This function now always returns "Deprecated".
    // Kept for backward compatibility with old Papyrus scripts that may
    // call it during migration to event-driven architecture.
    return "Deprecated";
}

static std::string Cancel(RE::StaticFunctionTag*) {
    auto& s = CaptureSession::GetSingleton();
    if (s.IsIdle()) return "Nothing to cancel";
    s.RequestCancel();
    return "Cancelled";
}

static std::string MYReset(RE::StaticFunctionTag*, bool force = true) {
    if (!force && !CaptureSession::GetSingleton().IsIdle())
        return "Cannot reset while active";
    CaptureSession::GetSingleton().ForceReset();
    // Restore UI directly via Scaleform (game thread)
    RestoreUIAsync();
    MenuEventSink::GetSingleton()->ClearCaptureToken();
    MenuEventSink::GetSingleton()->SetMenusHidden(false);
    return "Reset complete";
}

static bool CheckPath(RE::StaticFunctionTag*, std::string path) {
    if (path.empty() || path.size() < 3) return false;
    if (!((path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
          (path[0] == '\\' && path[1] == '\\'))) return false;
    try {
        std::filesystem::path fp(path);
        std::error_code ec;
        if (!std::filesystem::exists(fp, ec)) std::filesystem::create_directories(fp, ec);
        if (ec) return false;
        std::filesystem::path test = fp / "printscreen_test.tmp";
        std::ofstream f(test, std::ios::binary);
        if (!f.is_open()) return false;
        f.write("test", 4); f.close();
        std::filesystem::remove(test, ec);
        return true;
    } catch (...) { return false; }
}

// ============================================================
// Register
// ============================================================
bool PapyrusBindings::Register(RE::BSScript::IVirtualMachine* vm) {
    logger::info("=== Registering Papyrus functions ===");
    Initialize();

    int ok = 0, fail = 0;
    auto reg = [&](const char* name, auto fn) {
        try { vm->RegisterFunction(name, kScriptName, fn, true); ++ok; }
        catch (...) { logger::error("Failed to register '{}'", name); ++fail; }
    };

    reg("CheckPath",           CheckPath);
    reg("TakePhoto",           TakePhoto);
    // Get_Result is deprecated — Papyrus should use PrintScreenComplete mod event.
    // Kept registered so old scripts don't crash; returns "Deprecated".
    reg("Get_Result",          GetResult);
    reg("Cancel",              Cancel);
    reg("MYReset",             MYReset);
    reg("IsGamePaused",        IsGamePaused_Cached);

    logger::info("=== {} functions registered ({} failed) ===", ok, fail);
    return (fail == 0);
}

void PapyrusBindings::Initialize() {
    // Nothing extra needed — singletons are lazy-initialized
}

void PapyrusBindings::OnDataLoaded() {
    logger::info("PapyrusBindings::OnDataLoaded — registering MenuEventSink");
    auto* sink = MenuEventSink::GetSingleton();
    sink->Register();

    // Wire sink's cancel callback to the session
    // (The session sets the token on Start; sink holds a weak_ptr)
}

void PapyrusBindings::OnPostLoadGame() {
    logger::info("PapyrusBindings::OnPostLoadGame — cleaning up stale state");

    auto* sink = MenuEventSink::GetSingleton();

    // Only restore UI if it was left hidden by a prior capture session.
    // Restore UI directly via Scaleform (game thread).
    if (sink && sink->AreMenusHidden()) {
        logger::info("  Menus were hidden — restoring UI");
        RestoreUIAsync();
        sink->SetMenusHidden(false);
    }

    if (sink) {
        sink->ClearCaptureToken();
    }

    // Force-reset any stale session state
    CaptureSession::GetSingleton().ForceReset();
}
