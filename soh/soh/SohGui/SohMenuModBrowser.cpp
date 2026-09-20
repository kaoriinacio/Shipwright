#include "SohMenu.h"
#include <soh/Notification/Notification.h>
#include <soh/OTRGlobals.h>
#include <ship/Context.h>
#include <ship/window/Window.h>

#include <SDL2/SDL_net.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace SohGui {

using namespace UIWidgets;
using json = nlohmann::json;

// ---------- Estrutura de um mod vindo do GameBanana ----------
struct ModEntry {
    int         id = 0;
    std::string name;
    std::string profileUrl;
    std::string downloadUrl;
    std::string author;
    std::string category;
};

// ---------- Estado global do browser ----------
namespace ModBrowserState {
    static std::vector<ModEntry>  mods;
    static std::mutex             modsMutex;
    static std::atomic<bool>      fetching{ false };
    static std::atomic<bool>      fetchDone{ false };
    static std::string            lastError;
    static char                   searchBuf[128] = "";
}

// ---------- HTTP GET cru em cima de SDL_net (porta 80, sem TLS) ----------
// Retorna o corpo da resposta, ou "" em caso de erro (lastError preenchido).
static std::string HttpGetRaw(const std::string& host, const std::string& path) {
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, host.c_str(), 80) == -1) {
        ModBrowserState::lastError = std::string("DNS falhou: ") + SDLNet_GetError();
        return "";
    }

    TCPsocket sock = SDLNet_TCP_Open(&ip);
    if (!sock) {
        ModBrowserState::lastError = std::string("TCP open falhou: ") + SDLNet_GetError();
        return "";
    }

    // Monta request HTTP/1.1
    std::string req;
    req += "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "User-Agent: SoH-ModBrowser/0.1\r\n";
    req += "Accept: application/json\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";

    if (SDLNet_TCP_Send(sock, req.c_str(), (int)req.size()) < (int)req.size()) {
        ModBrowserState::lastError = "Send incompleto";
        SDLNet_TCP_Close(sock);
        return "";
    }

    // Le tudo ate o servidor fechar (Connection: close)
    std::string raw;
    char buf[4096];
    int n;
    while ((n = SDLNet_TCP_Recv(sock, buf, sizeof(buf))) > 0) {
        raw.append(buf, n);
    }
    SDLNet_TCP_Close(sock);

    // Separa headers de body
    auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        ModBrowserState::lastError = "Resposta HTTP malformada";
        return "";
    }

    std::string headers = raw.substr(0, headerEnd);
    std::string body    = raw.substr(headerEnd + 4);

    // Checa status code
    auto firstLineEnd = headers.find("\r\n");
    std::string statusLine = headers.substr(0, firstLineEnd);
    if (statusLine.find(" 200 ") == std::string::npos) {
        ModBrowserState::lastError = "HTTP nao-200: " + statusLine;
        return "";
    }

    // Se veio chunked, decodifica (GameBanana usa chunked as vezes)
    if (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
        headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        size_t pos = 0;
        while (pos < body.size()) {
            auto lineEnd = body.find("\r\n", pos);
            if (lineEnd == std::string::npos) break;
            std::string sizeStr = body.substr(pos, lineEnd - pos);
            size_t chunkSize = std::strtoul(sizeStr.c_str(), nullptr, 16);
            if (chunkSize == 0) break;
            pos = lineEnd + 2;
            if (pos + chunkSize > body.size()) break;
            decoded.append(body, pos, chunkSize);
            pos += chunkSize + 2;
        }
        return decoded;
    }

    return body;
}

// ---------- Parse do JSON do GameBanana pra lista de ModEntry ----------
static std::vector<ModEntry> ParseGameBananaMods(const std::string& body) {
    std::vector<ModEntry> result;
    try {
        json j = json::parse(body);
        // A resposta da Subfeed e um array de objetos com _sModelName, _idRow, _sName, etc.
        if (!j.is_array()) {
            ModBrowserState::lastError = "JSON nao e array";
            return result;
        }
        for (auto& item : j) {
            if (!item.contains("_sModelName")) continue;
            if (item["_sModelName"] != "Mod") continue;

            ModEntry m;
            if (item.contains("_idRow"))     m.id   = item["_idRow"].get<int>();
            if (item.contains("_sName"))     m.name = item["_sName"].get<std::string>();
            if (item.contains("_sProfileUrl")) m.profileUrl = item["_sProfileUrl"].get<std::string>();
            if (item.contains("_aSubmitter") && item["_aSubmitter"].contains("_sName"))
                m.author = item["_aSubmitter"]["_sName"].get<std::string>();
            if (item.contains("_aCategory") && item["_aCategory"].contains("_sName"))
                m.category = item["_aCategory"]["_sName"].get<std::string>();

            // Download: construimos a partir do id
            if (m.id > 0) {
                m.downloadUrl = "https://gamebanana.com/mods/download/" + std::to_string(m.id);
            }

            result.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Parse JSON falhou: ") + e.what();
    }
    return result;
}

// ---------- Worker que roda em thread separada ----------
static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::fetchDone = false;
    ModBrowserState::lastError.clear();

    // GameBanana: jogo 5689 = Ship of Harkinian
    const std::string host = "gamebanana.com";
    const std::string path = "/apiv11/Game/5689/Subfeed?_nPage=1&_sSort=default&_csvModelInclusions=Mod";

    std::string body = HttpGetRaw(host, path);

    if (!body.empty()) {
        auto mods = ParseGameBananaMods(body);
        {
            std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);
            ModBrowserState::mods = std::move(mods);
        }
        SPDLOG_INFO("[ModBrowser] {} mods carregados.", ModBrowserState::mods.size());
    } else {
        SPDLOG_ERROR("[ModBrowser] Falha no fetch: {}", ModBrowserState::lastError);
    }

    ModBrowserState::fetching = false;
    ModBrowserState::fetchDone = true;
}

// ---------- UI: lista de mods renderizada na sidebar ----------
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
        ImGui::TextDisabled("Nenhum mod carregado. Clica em 'Atualizar lista'.");
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
        if (!m.category.empty()) ImGui::SameLine(), ImGui::TextDisabled(" [%s]", m.category.c_str());

        if (ImGui::Button("Abrir pagina")) {
            // TODO Fase 5: usar SDL_OpenURL (SDL 2.0.14+)
            SPDLOG_INFO("[ModBrowser] Abrir: {}", m.profileUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Baixar")) {
            // TODO Fase 5: implementar download real
            SPDLOG_INFO("[ModBrowser] Baixar mod {} -> {}", m.id, m.downloadUrl);
            Notification::Emit({ .message = "Download ainda nao implementado (Fase 5)." });
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

    // Sidebar: Browse
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

    // Sidebar: Sobre
    path.sidebarName = "Sobre";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Info", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Mod Browser experimental para Ship of Harkinian.\n"
              "Fonte: GameBanana API v11.\n"
              "Inspirado no Mod Browser do Dusklight v2.0.0.\n"
              "Fase atual: 3/6 (fetch + listagem).",
              WIDGET_TEXT);
}

} // namespace SohGui
