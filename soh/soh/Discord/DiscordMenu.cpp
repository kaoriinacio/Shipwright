#include "DiscordMenu.h"

#include "soh/cvar_prefixes.h"
#include <libultraship/bridge/consolevariablebridge.h>

#include <imgui.h>

#define CVAR_DISCORD(name) CVAR_ENHANCEMENT("Discord." name)

namespace DiscordMenu {

static void Check(const char* cvar, const char* label) {
    bool value = CVarGetInteger(cvar, 0);
    if (ImGui::Checkbox(label, &value)) {
        CVarSetInteger(cvar, value ? 1 : 0);
    }
}

void Draw() {
    Check(CVAR_DISCORD("Enabled"), "Enable Rich Presence");

    if (!CVarGetInteger(CVAR_DISCORD("Enabled"), 1)) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1),
            "Discord presence is disabled.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Show on profile:");

    Check(CVAR_DISCORD("ShowArea"),      "Current area");
    Check(CVAR_DISCORD("ShowRoom"),      "Room / boss info");
    Check(CVAR_DISCORD("ShowGameTime"),  "In-game time (day/night)");
    Check(CVAR_DISCORD("ShowPlayTime"),  "Session time");
    Check(CVAR_DISCORD("ShowHealth"),    "Hearts");
    Check(CVAR_DISCORD("ShowAge"),       "Link's age");
    Check(CVAR_DISCORD("ShowEquipment"), "Equipment (sword / boots)");
    Check(CVAR_DISCORD("ShowProgress"),  "Quest progress (medallions / stones)");
    Check(CVAR_DISCORD("ShowRupees"),    "Rupees");

    ImGui::Separator();
    ImGui::Text("Update interval (frames):");

    int interval = CVarGetInteger(CVAR_DISCORD("UpdateInterval"), 30);
    if (ImGui::SliderInt("##discord_interval", &interval, 10, 300)) {
        CVarSetInteger(CVAR_DISCORD("UpdateInterval"), interval);
    }
}

} // namespace DiscordMenu
