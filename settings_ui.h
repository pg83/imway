#pragma once

struct Composer;
struct DialogState;
struct Settings;

// Plain pool-owned ImGui dialog. nullptr state means closed; toggle flips it.
void drawSettings(Composer& composer, Settings& settings, bool toggle, int& shortcutCapture, DialogState** state);
