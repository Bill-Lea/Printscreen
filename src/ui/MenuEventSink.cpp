#include "PCH.h"
#include "MenuEventSink.h"
#include "UIController.h"

// ============================================================
// Ignored menu list
// ============================================================
const std::set<std::string> MenuEventSink::kIgnoredMenus = {
    "Console", "Console Native UI Menu", "TweenMenu", "Fader Menu",
    "LoadWaitSpinner", "Cursor Menu", "HUD Menu", "Main Menu",
    "Loading Menu", "Mist Menu", "Overlay Menu",
    "Overlay Interaction Menu", "Debug Text Menu", "Safety Zone Menu"
};

// Menus that indicate the game is paused or text input is active
const std::set<std::string> MenuEventSink::kPauseOrInputMenus = {
    "InventoryMenu", "MagicMenu", "Journal Menu", "MapMenu",
    "FavoritesMenu", "Book Menu", "BarterMenu", "ContainerMenu",
    "Dialogue Menu", "GiftMenu", "Lockpicking Menu", "Sleep/Wait Menu",
    "StatsMenu", "Training Menu", "Tutorial Menu", "Console",
    "Console Native UI Menu"
};

// ============================================================
// Singleton
// ============================================================
MenuEventSink* MenuEventSink::GetSingleton() {
    static MenuEventSink instance;
    return &instance;
}

// ============================================================
// Register / Unregister
// ============================================================
void MenuEventSink::Register() {
    if (registered_.load()) return;
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink(this);
        registered_.store(true);
        logger::info("MenuEventSink: registered");
    } else {
        logger::error("MenuEventSink::Register: no UI singleton");
    }
}

void MenuEventSink::Unregister() {
    if (!registered_.load()) return;
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->RemoveEventSink(this);
        registered_.store(false);
        logger::info("MenuEventSink: unregistered");
    }
}

// ============================================================
// Token management
// ============================================================
void MenuEventSink::SetCaptureToken(CancellationToken::WeakPtr token) {
    std::lock_guard lock(tokenMutex_);
    captureToken_ = std::move(token);
}

void MenuEventSink::ClearCaptureToken() {
    std::lock_guard lock(tokenMutex_);
    captureToken_.reset();
}

// ============================================================
// ShouldIgnoreMenu
// ============================================================
bool MenuEventSink::ShouldIgnoreMenu(const RE::BSFixedString& name) const {
    std::string n = name.c_str();
    if (kIgnoredMenus.count(n)) return true;
    if (n.find("Cursor")  != std::string::npos) return true;
    if (n.find("Fader")   != std::string::npos) return true;
    if (n.find("Debug")   != std::string::npos) return true;
    if (n.find("Tween")   != std::string::npos) return true;
    return false;
}

// ============================================================
// IsPauseOrInputMenu
// ============================================================
bool MenuEventSink::IsPauseOrInputMenu(const RE::BSFixedString& name) const {
    std::string n = name.c_str();
    if (kPauseOrInputMenus.count(n)) return true;
    // Also check menu flags for pause/input
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    auto menu = ui->GetMenu(name);
    if (!menu) return false;
    auto* im = static_cast<RE::IMenu*>(menu.get());
    if (!im) return false;
    if (im->menuFlags.any(RE::UI_MENU_FLAGS::kPausesGame)) return true;
    if (im->menuFlags.any(RE::UI_MENU_FLAGS::kRequiresUpdate)) {
        // Text input menus typically require update
        return true;
    }
    return false;
}

// ============================================================
// ProcessEvent
// ============================================================
RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event || !event->opening)
        return RE::BSEventNotifyControl::kContinue;

    if (!menusHidden_.load())
        return RE::BSEventNotifyControl::kContinue;

    if (ShouldIgnoreMenu(event->menuName))
        return RE::BSEventNotifyControl::kContinue;

    // Update cached pause state based on any menu open/close
    if (event->opening) {
        isPaused_.store(IsPauseOrInputMenu(event->menuName));
    } else {
        isPaused_.store(false);
    }

    // Check if this menu indicates pause or text input
    bool isCriticalMenu = IsPauseOrInputMenu(event->menuName);

    // Try to cancel the active capture via the weak token
    CancellationToken::Ptr tok;
    {
        std::lock_guard lock(tokenMutex_);
        tok = captureToken_.lock();
    }

    if (!tok) {
        // No active capture — restore UI to be safe (direct Scaleform, game thread)
        logger::warn("MenuEventSink: menu '{}' opened with no active token, restoring UI",
                     event->menuName.c_str());
        UIController::GetSingleton().RestoreAllAsync();
        return RE::BSEventNotifyControl::kContinue;
    }

    if (isCriticalMenu) {
        logger::warn("MenuEventSink: critical menu '{}' opened during capture — cancelling and restoring UI",
                     event->menuName.c_str());
        tok->Cancel();
        UIController::GetSingleton().RestoreAllAsync();
    } else {
        logger::warn("MenuEventSink: menu '{}' opened during capture — cancelling",
                     event->menuName.c_str());
        tok->Cancel();
    }

    return RE::BSEventNotifyControl::kContinue;
}
