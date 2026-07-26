"""The single source of truth for imway runtime settings.

Defaults are C++ expressions unless the setting kind is ``text``. Widget
metadata drives the generated settings dialog; custom pages keep their
hand-written renderer but still describe every setting here.
"""


def checkbox(widget_id, *, visible_if=None):
    return {"kind": "checkbox", "id": widget_id, "visible_if": visible_if}


def slider(widget_id, low, high, fmt, *, visible_if=None):
    return {
        "kind": "slider",
        "id": widget_id,
        "low": low,
        "high": high,
        "format": fmt,
        "visible_if": visible_if,
    }


def combo(widget_id, items, *, visible_if=None):
    return {
        "kind": "combo",
        "id": widget_id,
        "items": items,
        "visible_if": visible_if,
    }


def text(widget_id, *, visible_if=None):
    return {"kind": "text", "id": widget_id, "visible_if": visible_if}


def multiline(widget_id, lines=6):
    return {"kind": "multiline", "id": widget_id, "lines": lines}


def color(widget_id, *, visible_if=None):
    return {"kind": "color", "id": widget_id, "visible_if": visible_if}


def time(widget_id, *, visible_if=None):
    return {"kind": "time", "id": widget_id, "visible_if": visible_if}


def custom(handler):
    return {"kind": "custom", "handler": handler}


def setting(key, name, cpp_type, default, group, label, widget, *,
            mode="value", normalize=None):
    return {
        "key": key,
        "name": name,
        "type": cpp_type,
        "default": default,
        "group": group,
        "label": label,
        "widget": widget,
        "mode": mode,
        "normalize": normalize,
    }


def text_setting(key, name, default, group, label, widget):
    return setting(key, name, "text", default, group, label, widget,
                   mode="text")


def array_setting(key, name, cpp_type, count, defaults, group, label, widget,
                  *, mode="ref", count_name=None):
    value = setting(key, name, cpp_type, defaults, group, label, widget,
                    mode=mode)
    value["count"] = count
    value["count_name"] = count_name or f"{name}Count"
    return value


PAGES = [
    {"name": "display"},
    {"name": "color"},
    {"name": "appearance"},
    {"name": "audio", "prefix": "drawAudioStatus"},
    {"name": "input"},
    {"name": "keyboard", "prefix": "drawActiveLayout"},
    {"name": "shortcuts"},
    {"name": "notifications"},
    {"name": "desktop"},
    {"name": "applications"},
    {"name": "advanced"},
]


SETTINGS = [
    # Display and power.
    setting("display.ui_scale", "uiScale", "float", "1.f", "display",
            "ui scale", custom("pageDisplay"), normalize=("1.f", "3.f")),
    setting("display.brightness_step", "brightnessStep", "float", ".05f",
            "display", "brightness step", custom("pageDisplay")),
    setting("display.hdr_step_nits", "hdrStepNits", "float", "10.f",
            "display", "hdr key step", custom("pageDisplay")),
    setting("display.osd_seconds", "osdSeconds", "float", "1.5f", "display",
            "osd duration", custom("pageDisplay")),
    setting("display.osd_fade_seconds", "osdFadeSeconds", "float", ".3f",
            "display", "osd fade", custom("pageDisplay")),
    setting("display.dpms_seconds", "dpmsSeconds", "double", "0.", "display",
            "display sleep", custom("pageDisplay")),
    setting("display.lock_seconds", "lockSeconds", "double", "0.", "display",
            "auto lock", custom("pageDisplay")),
    setting("display.lock_before_dpms", "lockBeforeDpms", "bool", "true",
            "display", "lock before sleep", custom("pageDisplay")),
    setting("display.hdr_enabled", "hdrEnabled", "bool", "false", "display",
            "hdr", custom("pageDisplay")),
    setting("display.sdr_nits", "sdrNits", "float", "80.f", "display",
            "sdr white", custom("pageDisplay")),
    setting("display.minimum_nits", "displayMinNits", "float", "0.f",
            "display", "display minimum", custom("pageDisplay")),
    setting("display.peak_nits", "displayPeakNits", "float", "0.f", "display",
            "display peak", custom("pageDisplay")),
    setting("display.max_fall_nits", "displayMaxFallNits", "float", "0.f",
            "display", "display maxFALL", custom("pageDisplay")),
    setting("display.bpc", "outputBpc", "u32", "0", "display",
            "bits per channel", custom("pageDisplay")),
    setting("display.range", "outputRange", "OutputRange",
            "OutputRange::automatic", "display", "rgb range",
            custom("pageDisplay")),
    text_setting("display.connector", "outputName", "", "display",
                 "connector", custom("pageDisplay")),
    text_setting("display.mode", "outputMode", "", "display", "mode",
                 custom("pageDisplay")),

    # Color and appearance.
    setting("color.night_light", "nightOn", "bool", "false", "color",
            "night light", checkbox("##night")),
    setting("color.temperature", "nightK", "float", "3400.f", "color",
            "temperature", slider("##night-k", "2500.f", "6500.f", "%.0f K")),
    setting("color.night_scheduled", "nightScheduled", "bool", "false",
            "color", "schedule", checkbox("##night-schedule")),
    setting("color.night_start", "nightStartMinute", "int", "20 * 60",
            "color", "starts",
            time("##night-start", visible_if="s.nightScheduled()")),
    setting("color.night_end", "nightEndMinute", "int", "7 * 60", "color",
            "ends", time("##night-end", visible_if="s.nightScheduled()")),
    setting("appearance.variant", "themeVariant", "ThemeVariant",
            "ThemeVariant::dark", "appearance", "variant",
            combo("##theme-variant", ["dark", "light", "system"])),
    setting("appearance.neutral", "neutral", "ThemeColor",
            "ThemeColor{.14f, .14f, .14f, 1.f}", "appearance", "neutral",
            color("##neutral"), mode="ref"),
    setting("appearance.selection", "selection", "ThemeColor",
            "ThemeColor{.26f, .59f, .98f, 1.f}", "appearance", "selection",
            color("##selection"), mode="ref"),
    text_setting("appearance.font", "fontPath", "", "appearance", "font",
                 text("##font")),
    setting("appearance.font_size", "fontSize", "float", "16.f",
            "appearance", "font size",
            slider("##font-size", "8.f", "32.f", "%.0f px")),
    text_setting("appearance.icon_theme", "iconTheme", "hicolor",
                 "appearance", "icon theme", text("##icon-theme")),
    setting("appearance.cursor_scale", "cursorScale", "float", "1.f",
            "appearance", "cursor scale",
            slider("##cursor-scale", ".5f", "3.f", "%.2f")),
    setting("appearance.window_shadows", "windowShadows", "bool", "true",
            "appearance", "window shadows", checkbox("##shadows")),
    setting("appearance.shadow_strength", "shadowStrength", "float", "1.f",
            "appearance", "shadow strength",
            slider("##shadow-strength", "0.f", "2.f", "%.2f",
                   visible_if="s.windowShadows()")),
    setting("appearance.visual_bell", "visualBell", "bool", "true",
            "appearance", "visual bell", checkbox("##visual-bell")),
    setting("appearance.visual_bell_seconds", "visualBellSeconds", "float",
            ".15f", "appearance", "bell duration",
            slider("##bell-seconds", ".02f", "1.f", "%.2f sec",
                   visible_if="s.visualBell()")),
    setting("appearance.visual_bell_strength", "visualBellStrength", "float",
            ".35f", "appearance", "bell strength",
            slider("##bell-strength", "0.f", "1.f", "%.2f",
                   visible_if="s.visualBell()")),
    setting("appearance.lock_blur", "lockBlur", "bool", "true", "appearance",
            "lock blur", checkbox("##lock-blur")),
    setting("appearance.lock_tint", "lockTint", "float", ".42f",
            "appearance", "lock tint",
            slider("##lock-tint", "0.f", "1.f", "%.2f")),

    # Audio.
    setting("audio.volume_step", "volumeStep", "float", ".05f", "audio",
            "key step", slider("##volume-step", ".01f", ".25f", "%.2f")),
    setting("audio.backend", "audioBackend", "BackendPreference",
            "BackendPreference::automatic", "audio", "backend",
            combo("##audio-backend", ["auto", "sndio", "pulse", "disabled"])),

    # Keyboard and input.
    text_setting("keyboard.layouts", "xkbLayouts", "us,ru", "keyboard",
                 "layouts", text("##layouts")),
    text_setting("keyboard.options", "xkbOptions", "grp:caps_toggle",
                 "keyboard", "xkb options", text("##xkb-options")),
    setting("keyboard.layout_policy", "layoutPolicy", "LayoutPolicy",
            "LayoutPolicy::perWindow", "keyboard", "layout scope",
            combo("##layout-policy", ["global", "per window"])),
    setting("keyboard.repeat_rate", "repeatRate", "int", "25", "keyboard",
            "repeat rate", slider("##repeat-rate", "1", "100", "%d Hz")),
    setting("keyboard.repeat_delay", "repeatDelay", "int", "600", "keyboard",
            "repeat delay", slider("##repeat-delay", "100", "2000", "%d ms")),
    setting("input.pointer_speed", "pointerSpeed", "float", "0.f", "input",
            "pointer speed", custom("pageInput"), normalize=("-1.f", "1.f")),
    setting("input.acceleration", "pointerAccelProfile",
            "PointerAccelProfile", "PointerAccelProfile::adaptive", "input",
            "acceleration", custom("pageInput")),
    setting("input.tap_to_click", "tapToClick", "bool", "true", "input",
            "tap to click", custom("pageInput")),
    setting("input.natural_scroll", "naturalScroll", "bool", "false",
            "input", "natural scroll", custom("pageInput")),
    setting("input.left_handed", "leftHanded", "bool", "false", "input",
            "left handed", custom("pageInput")),
    setting("input.disable_while_typing", "disableWhileTyping", "bool",
            "true", "input", "disable while typing", custom("pageInput")),
    setting("input.middle_emulation", "middleEmulation", "bool", "false",
            "input", "middle emulation", custom("pageInput")),
    setting("input.click_method", "touchpadClickMethod",
            "TouchpadClickMethod", "TouchpadClickMethod::automatic", "input",
            "click method", custom("pageInput")),
    setting("input.scroll_method", "touchpadScrollMethod",
            "TouchpadScrollMethod", "TouchpadScrollMethod::automatic", "input",
            "scroll method", custom("pageInput")),
    setting("input.swipe_left", "swipeLeft", "GestureAction",
            "GestureAction::none", "input", "swipe left", custom("pageInput")),
    setting("input.swipe_right", "swipeRight", "GestureAction",
            "GestureAction::none", "input", "swipe right", custom("pageInput")),
    setting("input.swipe_up", "swipeUp", "GestureAction",
            "GestureAction::none", "input", "swipe up", custom("pageInput")),
    setting("input.swipe_down", "swipeDown", "GestureAction",
            "GestureAction::none", "input", "swipe down", custom("pageInput")),
    setting("input.pinch_in", "pinchIn", "GestureAction",
            "GestureAction::none", "input", "pinch in", custom("pageInput")),
    setting("input.pinch_out", "pinchOut", "GestureAction",
            "GestureAction::none", "input", "pinch out", custom("pageInput")),
    array_setting("input.devices", "inputDevice", "InputDeviceSettings", 16,
                  "{}", "input", "input devices", custom("pageInput"),
                  count_name="inputDeviceCapacity"),
    setting("input.device_count", "inputDeviceCount", "size_t", "0", "input",
            "input device count", custom("pageInput")),
    array_setting(
        "shortcuts.bindings", "shortcut", "ShortcutBinding", 6,
        [
            "{ShortcutAction::screenshot, ~0u, XKB_KEY_Print}",
            "{ShortcutAction::lock, kModLogo, XKB_KEY_l}",
            "{ShortcutAction::launcher, kModLogo, XKB_KEY_F2}",
            "{ShortcutAction::inspector, kModLogo, XKB_KEY_F12}",
            "{ShortcutAction::altTabNext, kModAlt, XKB_KEY_Tab}",
            "{ShortcutAction::altTabPrev, kModAlt | kModShift, XKB_KEY_Tab}",
        ],
        "shortcuts", "bindings", custom("pageShortcuts"),
    ),

    # Desktop and window management.
    setting("desktop.item_is_menu", "trayMenuOnPrimary", "bool", "true",
            "desktop", "ItemIsMenu primary", checkbox("open DBusMenu")),
    setting("desktop.dock_visible", "dockVisible", "bool", "true", "desktop",
            "dock", checkbox("visible##dock")),
    setting("desktop.dock_position", "dockPosition", "DockPosition",
            "DockPosition::left", "desktop", "dock position",
            combo("##dock-position", ["left", "right", "top", "bottom"])),
    setting("desktop.dock_auto_hide", "dockAutoHide", "bool", "false",
            "desktop", "auto hide", checkbox("##dock-autohide")),
    setting("desktop.dock_width", "dockWidth", "float", "58.f", "desktop",
            "dock width", slider("##dock-width", "32.f", "128.f", "%.0f px")),
    setting("desktop.dock_icon_size", "dockIconSize", "float", "48.f",
            "desktop", "icon size",
            slider("##dock-icons", "16.f", "96.f", "%.0f px")),
    setting("desktop.group_windows", "dockGroupWindows", "bool", "true",
            "desktop", "group windows", checkbox("##dock-group")),
    setting("desktop.show_tray", "dockShowTray", "bool", "true", "desktop",
            "show tray", checkbox("##dock-tray")),
    setting("desktop.merge_tray", "dockMergeTray", "bool", "true", "desktop",
            "merge tray", checkbox("##dock-merge")),
    setting("desktop.mru_order", "dockMruOrder", "bool", "true", "desktop",
            "mru order", checkbox("##dock-mru")),
    setting("desktop.active_click", "dockClickAction", "DockClickAction",
            "DockClickAction::focus", "desktop", "active click",
            combo("##dock-click", ["focus", "minimize", "cycle"])),
    text_setting("desktop.pinned_apps", "dockPinned", "", "desktop",
                 "pinned apps", text("##dock-pinned")),
    setting("desktop.top_bar", "topBarVisible", "bool", "true", "desktop",
            "top bar", checkbox("visible##topbar")),
    setting("desktop.focused_app_id", "topBarAppId", "bool", "true",
            "desktop", "focused app id", checkbox("##topbar-app")),
    setting("desktop.global_menu", "topBarGlobalMenu", "bool", "true",
            "desktop", "global menu", checkbox("##global-menu")),
    setting("desktop.layout_indicator", "topBarLayout", "bool", "true",
            "desktop", "layout indicator", checkbox("##topbar-layout")),
    setting("desktop.wifi_indicator", "topBarWifi", "bool", "true", "desktop",
            "wifi indicator", checkbox("##topbar-wifi")),
    setting("desktop.battery", "topBarBattery", "BatteryDisplay",
            "BatteryDisplay::discharging", "desktop", "battery",
            combo("##battery", ["never", "when discharging", "always"])),
    setting("desktop.clock_24_hour", "clock24Hour", "bool", "true", "desktop",
            "24 hour clock", checkbox("##clock-24")),
    setting("desktop.clock_date", "clockShowDate", "bool", "true", "desktop",
            "clock date", checkbox("##clock-date")),
    setting("desktop.clock_seconds", "clockShowSeconds", "bool", "false",
            "desktop", "clock seconds", checkbox("##clock-seconds")),
    setting("desktop.clock_locale", "clockLocale", "bool", "true", "desktop",
            "locale formats", checkbox("##clock-locale")),
    setting("desktop.window_docking", "imguiDocking", "bool", "true",
            "desktop", "window docking", checkbox("##window-docking")),
    setting("desktop.focus_policy", "focusPolicy", "FocusPolicy",
            "FocusPolicy::click", "desktop", "focus",
            combo("##focus", ["click", "follows pointer"])),
    setting("desktop.raise_on_focus", "raiseOnFocus", "bool", "true",
            "desktop", "raise on focus", checkbox("##raise")),
    setting("desktop.decorations", "decorations", "DecorationPolicy",
            "DecorationPolicy::server", "desktop", "decorations",
            combo("##decorations",
                  ["server", "client", "client preference"])),
    setting("desktop.remember_layout", "rememberWindowLayout", "bool", "true",
            "desktop", "remember layout", checkbox("##remember-layout")),

    # Notifications.
    setting("notifications.dnd", "dnd", "bool", "false", "notifications",
            "do not disturb", custom("pageNotifications")),
    setting("notifications.dnd_scheduled", "dndScheduled", "bool", "false",
            "notifications", "dnd schedule", custom("pageNotifications")),
    setting("notifications.dnd_start", "dndStartMinute", "int", "22 * 60",
            "notifications", "starts", custom("pageNotifications")),
    setting("notifications.dnd_end", "dndEndMinute", "int", "7 * 60",
            "notifications", "ends", custom("pageNotifications")),
    setting("notifications.timeout", "notificationSeconds", "float", "5.f",
            "notifications", "default timeout", custom("pageNotifications")),
    setting("notifications.history", "notificationHistory", "int", "50",
            "notifications", "history limit", custom("pageNotifications")),
    setting("notifications.width", "notificationWidth", "float", "320.f",
            "notifications", "toast width", custom("pageNotifications")),
    setting("notifications.position", "toastPosition", "ToastPosition",
            "ToastPosition::topRight", "notifications", "toast position",
            custom("pageNotifications")),
    setting("notifications.wifi", "notifyWifi", "bool", "true",
            "notifications", "wifi events", custom("pageNotifications")),
    setting("notifications.critical", "allowCriticalNotifications", "bool",
            "true", "notifications", "critical notifications",
            custom("pageNotifications")),
    array_setting("notifications.rules", "notificationRule",
                  "NotificationRule", 32, "{}", "notifications",
                  "application rules", custom("pageNotifications"),
                  count_name="notificationRuleCapacity"),
    setting("notifications.rule_count", "notificationRuleCount", "size_t",
            "0", "notifications", "application rule count",
            custom("pageNotifications")),

    # Applications and actions.
    text_setting("applications.terminal", "terminal", "zutty",
                 "applications", "terminal", text("##terminal")),
    text_setting("applications.terminal_exec", "terminalExec", "-e sh -c",
                 "applications", "terminal exec", text("##terminal-exec")),
    text_setting("applications.autostart", "autostart", "", "applications",
                 "autostart commands", multiline("##autostart")),
    setting("applications.launcher_shell", "launcherShellCommands", "bool",
            "true", "applications", "launcher shell",
            checkbox("##launcher-shell")),
    setting("applications.screenshot_action", "screenshotAction",
            "ScreenshotAction", "ScreenshotAction::editor", "applications",
            "screenshot action",
            combo("##screenshot-action", ["editor", "save", "copy"])),
    text_setting("applications.screenshot_directory", "screenshotDirectory",
                 "", "applications", "screenshot directory",
                 text("##screenshot-dir")),
    text_setting("applications.screenshot_name", "screenshotName",
                 "imway-%Y%m%d-%H%M%S", "applications",
                 "filename template", text("##screenshot-name")),
    setting("applications.screenshot_format", "screenshotFormat",
            "ScreenshotFormat", "ScreenshotFormat::jxl", "applications",
            "format", combo("##screenshot-format", ["jxl", "png"])),
    setting("applications.screenshot_lossless", "screenshotLossless", "bool",
            "true", "applications", "lossless",
            checkbox("##screenshot-lossless")),
    setting("applications.screenshot_quality", "screenshotQuality", "float",
            "90.f", "applications", "quality",
            slider("##screenshot-quality", "1.f", "100.f", "%.0f%%",
                   visible_if="!s.screenshotLossless()")),
    setting("applications.wifi_backend", "wifiBackend", "BackendPreference",
            "BackendPreference::automatic", "applications", "wifi backend",
            combo("##wifi-backend",
                  ["auto", "iwd", "NetworkManager", "disabled"])),

    # Advanced and deployment policy.
    setting("advanced.direct_scanout", "directScanout", "bool", "true",
            "advanced", "direct scanout", checkbox("##direct-scanout")),
    setting("advanced.tearing", "tearing", "TearingPolicy",
            "TearingPolicy::client", "advanced", "tearing",
            combo("##tearing", ["deny", "client requested", "always"])),
    setting("advanced.hardware_cursor", "hardwareCursor", "bool", "true",
            "advanced", "hardware cursor", checkbox("##hardware-cursor")),
    setting("advanced.dithering", "dithering", "bool", "true", "advanced",
            "dithering", checkbox("##dithering")),
    setting("advanced.anr_seconds", "anrSeconds", "float", "10.f",
            "advanced", "unresponsive after",
            slider("##anr", "1.f", "60.f", "%.1f sec")),
    setting("advanced.seat_backend", "seatBackend", "SeatBackend",
            "SeatBackend::automatic", "advanced", "seat backend",
            combo("##seat", ["auto", "libseat", "direct"])),
    text_setting("advanced.pam_service", "pamService", "login", "advanced",
                 "pam service", text("##pam")),
]
