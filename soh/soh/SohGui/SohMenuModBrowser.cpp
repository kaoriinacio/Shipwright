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

#include <SDL2/SDL.h>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>

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

    // Estado do download
    static std::atomic<bool>     downloading{ false };
    static std::atomic<int>      downloadModId{ 0 };
    static std::mutex            downloadMutex;
    static std::string           downloadModName;
    static std::atomic<size_t>   downloadBytes{ 0 };
    static std::atomic<size_t>   downloadTotal{ 0 };
}

// ---------- Helpers ----------

static std::filesystem::path GetModsPath() {
    std::filesystem::path p;
    const char* base = SDL_GetBasePath();
    if (base) {
        p = std::filesystem::path(base) / "mods";
    } else {
        p = "mods";
    }
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

static std::string SanitizeFilename(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"'  || c == '<' || c == '>' || c == '|') {
            out += '_';
        } else {
            out += c;
        }
    }
    // Remove espacos nas pontas
    while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
    while (!out.empty() && (out.back()  == ' ' || out.back()  == '.')) out.pop_back();
    if (out.empty()) out = "mod";
    return out;
}

static std::string FormatBytes(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

// ---------- HTTP GET via httplib ----------
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
        SPDLOG_INFO("[ModBrowser] Resposta (primeiros 800 chars): {}",
                    body.substr(0, std::min<size_t>(800, body.size())));

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

// ---------- Fetch em thread ----------
static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::lastError.clear();

    const std::string host = "gamebanana.com";
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

// ---------- Download em thread ----------
static void DownloadModAsync(int modId, std::string modName) {
    ModBrowserState::downloading = true;
    ModBrowserState::downloadModId = modId;
    {
        std::lock_guard<std::mutex> lock(ModBrowserState::downloadMutex);
        ModBrowserState::downloadModName = modName;
    }
    ModBrowserState::downloadBytes = 0;
    ModBrowserState::downloadTotal = 0;

    try {
        httplib::Client cli("https://gamebanana.com");
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(120, 0);
        cli.set_write_timeout(120, 0);
        cli.enable_server_certificate_verification(false);

        std::filesystem::path modsPath = GetModsPath();
        std::filesystem::path dest = modsPath / (SanitizeFilename(modName) + ".zip");
        std::ofstream out(dest, std::ios::binary);
        if (!out) {
            ModBrowserState::lastError = "Nao consegui abrir arquivo pra escrita";
            Notification::Emit({ .message = "Erro: nao consegui salvar arquivo" });
            ModBrowserState::downloading = false;
            return;
        }

        std::string url = "/mods/download/" + std::to_string(modId);

        auto res = cli.Get(url.c_str(),
            [&](const char* data, size_t len) {
                out.write(data, len);
                ModBrowserState::downloadBytes += len;
                return true;
            },
            [&](uint64_t current, uint64_t total) {
                if (total > 0) ModBrowserState::downloadTotal = (size_t)total;
                return true;
            });
        out.close();

        if (res && res->status == 200) {
            Notification::Emit({ .message = "Baixado: " + modName });
            SPDLOG_INFO("[ModBrowser] Baixado {} -> {}", modName, dest.string());
        } else {
            std::string st = res ? std::to_string(res->status) : "sem resposta";
            ModBrowserState::lastError = "Download falhou (HTTP " + st + ")";
            std::error_code ec;
            std::filesystem::remove(dest, ec);
            Notification::Emit({ .message = "Erro ao baixar " + modName });
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Excecao download: ") + e.what();
        Notification::Emit({ .message = "Excecao: " + std::string(e.what()) });
    }

    ModBrowserState::downloading = false;
    ModBrowserState::downloadModId = 0;
}

// ---------- UI ----------
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

    // Barra de progresso do download
    if (ModBrowserState::downloading) {
        std::string nameCopy;
        {
            std::lock_guard<std::mutex> dl(ModBrowserState::downloadMutex);
            nameCopy = ModBrowserState::downloadModName;
        }
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Baixando: %s", nameCopy.c_str());
        size_t cur = ModBrowserState::downloadBytes;
        size_t tot = ModBrowserState::downloadTotal;
        if (tot > 0) {
            float frac = (float)cur / (float)tot;
            ImGui::ProgressBar(frac, ImVec2(-1, 0),
                (FormatBytes(cur) + " / " + FormatBytes(tot)).c_str());
        } else {
            ImGui::Text("Recebido: %s", FormatBytes(cur).c_str());
        }
        ImGui::Separator();
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

        // Abrir pagina
        if (ImGui::Button("Abrir pagina")) {
            if (!m.profileUrl.empty()) {
                SDL_OpenURL(m.profileUrl.c_str());
            }
        }
        ImGui::SameLine();

        // Baixar
        bool downloadingThis = ModBrowserState::downloading &&
                               ModBrowserState::downloadModId == m.id;
        bool anyDownloading = ModBrowserState::downloading;
        ImGui::BeginDisabled(anyDownloading);
        if (ImGui::Button(downloadingThis ? "Baixando..." : "Baixar")) {
            std::thread(DownloadModAsync, m.id, m.name).detach();
        }
        ImGui::EndDisabled();

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
              "Fase 5/6 (fetch + listagem + download).\n"
              "\n"
              "Os mods baixados vao pra pasta 'mods/' ao lado do executavel.",
              WIDGET_TEXT);
}

} // namespace SohGui
