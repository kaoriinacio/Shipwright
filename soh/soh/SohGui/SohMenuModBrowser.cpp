#include "SohMenu.h"
#include <soh/Notification/Notification.h>
#include <soh/OTRGlobals.h>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>

namespace SohGui {

using namespace UIWidgets;
using json = nlohmann::json;

struct ModEntry {
    int         id = 0;
    std::string name;
    std::string profileUrl;
    std::string downloadUrl;
    std::string author;
    std::string category;
};

namespace ModBrowserState {
    static std::vector<ModEntry> mods;
    static std::mutex            modsMutex;
    static std::atomic<bool>     fetching{ false };
    static std::string           lastError;
    static char                  searchBuf[128] = "";
}

static std::string HttpGet(const std::string& host, const std::string& path) {
    try {
        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(20);
        cli.enable_server_certificate_verification(false);

        auto res = cli.Get(path.c_str());
        if (!res) {
            ModBrowserState::lastError = "Falha: " + httplib::to_string(res.error());
            return "";
        }
        if (res->status != 200) {
            ModBrowserState::lastError = "HTTP " + std::to_string(res->status);
            return "";
        }
        return res->body;
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Excecao: ") + e.what();
        return "";
    }
}

static std::vector<ModEntry> ParseGameBananaMods(const std::string& body) {
    std::vector<ModEntry> result;
    try {
        json j = json::parse(body);
        if (!j.is_array()) {
            ModBrowserState::lastError = "JSON nao e array";
            return result;
        }
        for (auto& item : j) {
            if (!item.contains("_sModelName")) continue;
            if (item["_sModelName"] != "Mod") continue;
            ModEntry m;
            if (item.contains("_idRow"))       m.id = item["_idRow"].get<int>();
            if (item.contains("_sName"))       m.name = item["_sName"].get<std::string>();
            if (item.contains("_sProfileUrl")) m.profileUrl = item["_sProfileUrl"].get<std::string>();
            if (item.contains("_aSubmitter") && item["_aSubmitter"].contains("_sName"))
                m.author = item["_aSubmitter"]["_sName"].get<std::string>();
            if (item.contains("_aCategory") && item["_aCategory"].contains("_sName"))
                m.category = item["_aCategory"]["_sName"].get<std::string>();
            if (m.id > 0)
                m.downloadUrl = "https://gamebanana.com/mods/download/" + std::to_string(m.id);
            result.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Parse JSON falhou: ") + e.what();
    }
    return result;
}

static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::lastError.clear();
    const std::string host = "gamebanana.com";
    const std::string path = "/apiv11/Game/5689/Subfeed?_nPage=1&_sSort=default&_csvModelInclusions=Mod";
    std::string body = HttpGet(host, path);
    if (!body.empty()) {
        auto mods = ParseGameBananaMods(body);
        {
            std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);
            ModBrowserState::mods = std::move(mods);
        }
        SPDLOG_INFO("[ModBrowser] {} mods carregados.", ModBrowserState::mods.size());
    } else {
        SPDLOG_ERROR("[ModBrowser] Falha: {}", ModBrowserState::lastError);
    }
    ModBrowserState::fetching = false;
}

static void DrawModList(WidgetInfo& info) {
    std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);
    if (ModBrowserState::fetching) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Carregando mods...");
        return;
    }
    if (!ModBrowserState::lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Erro: %s", ModBrowserState::lastError.c_str());
    }
    if (ModBrowserState::mods.empty()) {
        ImGui::TextDisabled("Nenhum mod. Clica em 'Atualizar lista'.");
        return;
    }
    ImGui::Text("%zu mods encontrados", ModBrowserState::mods.size());
    ImGui::Separator();
    std::string filter = ModBrowserState::searchBuf;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    ImGui::BeginChild("ModListScroll", ImVec2(0, 400), true);
    for (auto& m : ModBrowserState::mods) {
        if (!filter.empty()) {
            std::string lower = m.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(filter) == std::string::npos) continue;
        }
        ImGui::PushID(m.id);
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", m.name.c_str());
        if (!m.author.empty()) ImGui::TextDisabled("por %s", m.author.c_str());
        if (!m.category.empty()) { ImGui::SameLine(); ImGui::TextDisabled(" [%s]", m.category.c_str()); }
        if (ImGui::Button("Abrir pagina")) SPDLOG_INFO("[ModBrowser] Abrir: {}", m.profileUrl);
        ImGui::SameLine();
        if (ImGui::Button("Baixar")) {
            SPDLOG_INFO("[ModBrowser] Baixar {} -> {}", m.id, m.downloadUrl);
            Notification::Emit({ .message = "Download: Fase 5." });
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void SohMenu::AddMenuModBrowser() {
    AddMenuEntry("Mod Browser", CVAR_SETTING("Menu.ModBrowserSidebarSection"));
    WidgetPath path;

    path = { "Mod Browser", "Browse", SECTION_COLUMN_1 };
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);

    AddWidget(path, "Acoes", WIDGET_SEPARATOR_TEXT);

    AddWidget(path, "Atualizar lista de mods", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            if (ModBrowserState::fetching) return;
            std::thread(FetchModsAsync).detach();
            Notification::Emit({ .message = "Buscando mods no GameBanana..." });
        });

    AddWidget(path, "Filtrar", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "##ModBrowserFilter", WIDGET_CUSTOM).CustomFunction([](WidgetInfo& info) {
        ImGui::InputTextWithHint("##ModBrowserFilter", "Buscar por nome...",
                                 ModBrowserState::searchBuf, IM_ARRAYSIZE(ModBrowserState::searchBuf));
    });

    AddWidget(path, "Resultados", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "##ModBrowserList", WIDGET_CUSTOM).CustomFunction(DrawModList);

    path.sidebarName = "Sobre";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Info", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Mod Browser experimental para SoH.\n"
              "Fonte: GameBanana API v11.\n"
              "Fase 3/6.",
              WIDGET_TEXT);
}

} // namespace SohGui
