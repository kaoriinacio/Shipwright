#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0A00
    #endif
#endif

#include "SohMenu.h"
#include <soh/Notification/Notification.h>
#include <soh/OTRGlobals.h>

#include <spdlog/spdlog.h>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <exception>

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

// ---------- HTTP GET via httplib (HTTPS + follow redirect) ----------
static std::string HttpGet(const std::string& host, const std::string& path) {
    try {
        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(20, 0);
        cli.set_write_timeout(20, 0);
        cli.enable_server_certificate_verification(false);

        auto res = cli.Get(path.c_str());
        if (!res) {
            ModBrowserState::lastError = "Falha request (err code " +
                std::to_string(static_cast<int>(res.error())) + ")";
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
    } catch (...) {
        ModBrowserState::lastError = "Excecao desconhecida";
        return "";
    }
}

// ---------- Parse do JSON do GameBanana ----------
static std::vector<ModEntry> ParseGameBananaMods(const std::string& body) {
    std::vector<ModEntry> result;
    try {
        json j = json::parse(body);

        // DEBUG: loga o que veio (primeiros 800 chars)
        SPDLOG_INFO("[ModBrowser] Resposta (primeiros 800 chars): {}",
                    body.substr(0, std::min<size_t>(800, body.size())));

        // A API pode retornar um array direto OU um objeto com um array dentro.
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object()) {
            for (auto& [key, value] : j.items()) {
                if (value.is_array()) {
                    arr = &value;
                    SPDLOG_INFO("[ModBrowser] Usando array da chave '{}'", key);
                    break;
                }
            }
        }

        if (!arr) {
            ModBrowserState::lastError = "JSON nao contem array de mods";
            return result;
        }

        for (auto& item : *arr) {
            if (!item.is_object()) continue;

            ModEntry m;
            if (item.contains("_idRow"))       m.id = item["_idRow"].get<int>();
            if (item.contains("_sName"))       m.name = item["_sName"].get<std::string>();
            if (item.contains("_sProfileUrl")) m.profileUrl = item["_sProfileUrl"].get<std::string>();

            if (item.contains("_aSubmitter") && item["_aSubmitter"].is_object() &&
                item["_aSubmitter"].contains("_sName"))
                m.author = item["_aSubmitter"]["_sName"].get<std::string>();
            else if (item.contains("_aOwner") && item["_aOwner"].is_object() &&
                     item["_aOwner"].contains("_sName"))
                m.author = item["_aOwner"]["_sName"].get<std::string>();

            if (item.contains("_aCategory") && item["_aCategory"].is_object() &&
                item["_aCategory"].contains("_sName"))
                m.category = item["_aCategory"]["_sName"].get<std::string>();

            if (m.id > 0)
                m.downloadUrl = "https://gamebanana.com/mods/download/" + std::to_string(m.id);

            result.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Parse JSON: ") + e.what();
    }
    return result;
}

// ---------- Worker em thread ----------
static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::lastError.clear();

    const std::string host = "gamebanana.com";
    // ID do Ship of Harkinian no GameBanana: 16121
    const std::string path = "/apiv11/Game/16121/Subfeed?_nPage=1&_sSort=default&_csvModelInclusions=Mod";

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

// ---------- UI: lista de mods ----------
static void DrawModList(WidgetInfo& info) {
    std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);

    if (ModBrowserState::fetching) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Carregando mods...");
        return;
    }
    if (!ModBrowserState::lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Erro: %s",
                           ModBrowserState::lastError.c_str());
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
        if (!m.author.empty())   ImGui::TextDisabled("por %s", m.author.c_str());
        if (!m.category.empty()) { ImGui::SameLine(); ImGui::TextDisabled(" [%s]", m.category.c_str()); }

        if (ImGui::Button("Abrir pagina")) {
            SPDLOG_INFO("[ModBrowser] Abrir: {}", m.profileUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Baixar")) {
            SPDLOG_INFO("[ModBrowser] Baixar {} -> {}", m.id, m.downloadUrl);
            Notification::Emit({ .message = "Download: Fase 5 ainda nao implementada." });
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---------- Registro do menu ----------
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
              "Fase 3/6 (fetch + listagem).",
              WIDGET_TEXT);
}

} // namespace SohGui
