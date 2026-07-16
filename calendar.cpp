#include "calendar.h"
#include "util.h"

#include <time.h>

#include <imgui.h>

using namespace stl;

namespace {
    constexpr const char* kMonths[12] = {"january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"};
    constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // dialog-scoped state: one heap instance per dialog, newed on open
    // (seeded with the current month), deleted when the dialog ends — the
    // opaque handle behind the caller's void* slot
    struct Dialog {
        bool fresh = true;
        int year = 0;
        int mon = 0;

        Dialog();

        // pure drawing: state transitions stay in drawCalendar, the only
        // outward sign is the open flag dropping
        void draw(int screenW, bool& open);
    };
}

Dialog::Dialog() {
    time_t nowT = time(nullptr);
    tm lt{};

    localtime_r(&nowT, &lt);
    year = lt.tm_year + 1900;
    mon = lt.tm_mon;
}

void Dialog::draw(int screenW, bool& open) {
    ImGui::SetNextWindowPos(ImVec2((float)screenW - 8.f, ImGui::GetFrameHeight() + 4.f), ImGuiCond_Always, ImVec2(1.f, 0.f));

    if (ImGui::Begin("##calendar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        } else if (!ImGui::IsWindowFocused()) {
            open = false;
        }

        float cell = ImGui::GetFontSize() * 2.2f;

        if (ImGui::ArrowButton("##pm", ImGuiDir_Left)) {
            if (--mon < 0) {
                mon = 11;
                year--;
            }
        }

        auto& hdr = sb();

        hdr << kMonths[mon] << " "_sv << year;

        float hw = ImGui::CalcTextSize(hdr.cStr()).x;

        ImGui::SameLine((cell * 7.f - hw) / 2.f);
        ImGui::TextUnformatted(hdr.cStr());
        ImGui::SameLine(cell * 7.f - ImGui::GetFrameHeight());

        if (ImGui::ArrowButton("##nm", ImGuiDir_Right)) {
            if (++mon > 11) {
                mon = 0;
                year++;
            }
        }

        static const char* kWd[7] = {"mo", "tu", "we", "th", "fr", "sa", "su"};

        for (int i = 0; i < 7; i++) {
            if (i) {
                ImGui::SameLine((float)i * cell + ImGui::GetStyle().WindowPadding.x);
            }

            ImGui::TextDisabled("%s", kWd[i]);
        }

        bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        int days = kDays[mon] + (mon == 1 && leap ? 1 : 0);
        tm f{};

        f.tm_year = year - 1900;
        f.tm_mon = mon;
        f.tm_mday = 1;
        f.tm_hour = 12;
        mktime(&f);

        int col = (f.tm_wday + 6) % 7; // monday-based
        time_t nowT = time(nullptr);
        tm today{};

        localtime_r(&nowT, &today);

        // a mid-week day 1 must open its own row: its SameLine below would
        // otherwise glue the first week onto the weekday header line
        if (col) {
            ImGui::Dummy(ImVec2(0.f, 0.f));
        }

        for (int day = 1; day <= days; day++) {
            if (col) {
                ImGui::SameLine((float)col * cell + ImGui::GetStyle().WindowPadding.x);
            }

            auto& ds = sb();

            ds << day;

            bool isToday = today.tm_year + 1900 == year && today.tm_mon == mon && today.tm_mday == day;

            if (isToday) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 60, 255));
            }

            ImGui::TextUnformatted(ds.cStr());

            if (isToday) {
                ImGui::PopStyleColor();
            }

            if (++col == 7) {
                col = 0;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();
}

void drawCalendar(int screenW, bool toggle, void** state) {
    Dialog*& dp = *(Dialog**)state;

    if (toggle) {
        if (dp) {
            delete dp;
            dp = nullptr;
        } else {
            dp = new Dialog();
        }
    }

    if (!dp) {
        return;
    }

    bool open = true;

    dp->draw(screenW, open);

    if (!open) {
        delete dp;
        dp = nullptr;
    }
}
