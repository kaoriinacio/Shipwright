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
#include <SDL2/SDL_opengl.h>
#include <zip.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <chrono>
#include <deque>

// stb_image — usa declaracoes externas pra evitar conflito com outra TU
extern "C" unsigned char* stbi_load_from_memory(
    const unsigned char* buffer, int len,
    int* x, int* y, int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

#ifndef GL_CLAMP_TO_EDGE
    #define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace SohGui {

using namespace UIWidgets;
using json = nlohmann::json;

// ============================================================
// Estado global
// ============================================================

enum class ImageState : int { NotLoaded = 0, Loading = 1, Loaded = 2, Failed = 3 };

struct ModEntry {
    int         id = 0;
    std::string name;
    std::string profileUrl;
    std::string downloadUrl;
    std::string author;
    std::string category;
    std::string thumbUrl;   // URL da thumbnail
    std::string description; // descricao (preenchida on-demand)

    // Estado da imagem (uso interno)
    ImageState  imgState = ImageState::NotLoaded;
    unsigned int glTexture = 0;
};

struct ImagePayload {
    int modId = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels; // RGBA
    bool ok = false;
};

namespace ModBrowserState {
    static std::vector<ModEntry> mods;
    static std::mutex            modsMutex;
    static std::atomic<bool>     fetching{ false };
    static std::string           lastError;
    static char                  searchBuf[128] = "";
    static int                   selectedModId = 0;

    // Download
    static std::atomic<bool>     downloading{ false };
    static std::atomic<int>      downloadModId{ 0 };
    static std::mutex            downloadMutex;
    static std::string           downloadModName;
    static std::atomic<size_t>   downloadBytes{ 0 };
    static std::atomic<size_t>   downloadTotal{ 0 };

    // Imagens: fila de downloads prontos pra subir pra GPU
    static std::mutex            imageQueueMutex;
    static std::deque<ImagePayload> imageQueue;
    static std::atomic<bool>     imagesDownloading{ false };

    // Detalhes
    static std::atomic<bool>     detailsFetching{ false };
    static std::mutex            detailsMutex;
    static std::string           detailsText;
    static int                   detailsModId = 0;

    // Cache
    static std::atomic<bool>     cacheLoaded{ false };
}

// ============================================================
// Helpers
// ============================================================

static std::filesystem::path GetModsPath() {
    std::filesystem::path p;
    const char* base = SDL_GetBasePath();
    if (base) p = std::filesystem::path(base) / "mods";
    else      p = "mods";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

static std::filesystem::path GetCachePath() {
    return GetModsPath() / ".modbrowser_cache.json";
}

static std::string SanitizeFilename(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"'  || c == '<' || c == '>' || c == '|')
            out += '_';
        else
            out += c;
    }
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

// ============================================================
// Cache local do JSON de mods
// ============================================================

static void SaveCache(const std::vector<ModEntry>& mods) {
    try {
        json j;
        j["timestamp"] = (long long)std::time(nullptr);
        j["mods"] = json::array();
        for (auto& m : mods) {
            j["mods"].push_back({
                {"id", m.id},
                {"name", m.name},
                {"profileUrl", m.profileUrl},
                {"downloadUrl", m.downloadUrl},
                {"author", m.author},
                {"category", m.category},
                {"thumbUrl", m.thumbUrl}
            });
        }
        std::ofstream out(GetCachePath());
        out << j.dump();
        SPDLOG_INFO("[ModBrowser] Cache salvo ({} mods)", mods.size());
    } catch (const std::exception& e) {
        SPDLOG_WARN("[ModBrowser] Falha ao salvar cache: {}", e.what());
    }
}

static bool LoadCache(std::vector<ModEntry>& out) {
    try {
        std::ifstream in(GetCachePath());
        if (!in) return false;
        json j; in >> j;
        if (!j.contains("timestamp") || !j.contains("mods")) return false;
        long long ts = j["timestamp"].get<long long>();
        // Cache valido por 6h
        long long now = (long long)std::time(nullptr);
        if (now - ts > 6 * 3600) {
            SPDLOG_INFO("[ModBrowser] Cache expirado");
            return false;
        }
        for (auto& item : j["mods"]) {
            ModEntry m;
            m.id = item.value("id", 0);
            m.name = item.value("name", "");
            m.profileUrl = item.value("profileUrl", "");
            m.downloadUrl = item.value("downloadUrl", "");
            m.author = item.value("author", "");
            m.category = item.value("category", "");
            m.thumbUrl = item.value("thumbUrl", "");
            out.push_back(std::move(m));
        }
        SPDLOG_INFO("[ModBrowser] Cache carregado ({} mods)", out.size());
        return !out.empty();
    } catch (const std::exception& e) {
        SPDLOG_WARN("[ModBrowser] Falha ao ler cache: {}", e.what());
        return false;
    }
}

// ============================================================
// HTTP GET
// ============================================================

static std::string HttpGet(const std::string& host, const std::string& path) {
    try {
        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(20, 0);
        cli.set_write_timeout(20, 0);
        cli.enable_server_certificate_verification(false);

        auto res = cli.Get(path.c_str());
        if (!res) { ModBrowserState::lastError = "Falha request"; return ""; }
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

// ============================================================
// Parse JSON
// ============================================================

static std::vector<ModEntry> ParseGameBananaMods(const std::string& body) {
    std::vector<ModEntry> result;
    try {
        json j = json::parse(body);

        const json* arr = nullptr;
        if (j.is_array()) arr = &j;
        else if (j.is_object()) {
            for (auto& [key, value] : j.items()) {
                if (value.is_array()) { arr = &value; break; }
            }
        }
        if (!arr) { ModBrowserState::lastError = "JSON sem array"; return result; }

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

            // Thumbnail
            if (item.contains("_aPreviewMedia") && item["_aPreviewMedia"].is_object()) {
                auto& pm = item["_aPreviewMedia"];
                if (pm.contains("_aImages") && pm["_aImages"].is_array() && !pm["_aImages"].empty()) {
                    auto& img = pm["_aImages"][0];
                    std::string base = img.value("_sBaseUrl", "");
                    std::string file = img.value("_sFile220", img.value("_sFile", ""));
                    if (!base.empty() && !file.empty())
                        m.thumbUrl = base + "/" + file;
                }
            }

            if (m.id > 0)
                m.downloadUrl = "https://gamebanana.com/mods/download/" + std::to_string(m.id);

            result.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Parse: ") + e.what();
    }
    return result;
}

// ============================================================
// Fetch de mods
// ============================================================

static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::lastError.clear();

    const std::string path = "/apiv11/Game/16121/Subfeed?_nPage=1&_sSort=default&_csvModelInclusions=Mod";
    std::string body = HttpGet("gamebanana.com", path);

    if (!body.empty()) {
        auto mods = ParseGameBananaMods(body);
        {
            std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);
            ModBrowserState::mods = std::move(mods);
        }
        SaveCache(ModBrowserState::mods);
        SPDLOG_INFO("[ModBrowser] {} mods carregados.", ModBrowserState::mods.size());
    } else {
        SPDLOG_ERROR("[ModBrowser] Falha: {}", ModBrowserState::lastError);
    }
    ModBrowserState::fetching = false;
}

// ============================================================
// Download de thumbnails (fila de downloads em serie)
// ============================================================

static void DownloadThumbnail(int modId, std::string url) {
    if (url.empty()) return;
    try {
        // URL é tipo https://images.gamebanana.com/img/ss/mods/...
        std::string proto = "https://";
        if (url.rfind(proto, 0) == 0) url = url.substr(proto.size());
        auto slash = url.find('/');
        if (slash == std::string::npos) return;
        std::string host = url.substr(0, slash);
        std::string path = url.substr(slash);

        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(20, 0);
        cli.enable_server_certificate_verification(false);

        auto res = cli.Get(path.c_str());
        if (!res || res->status != 200) {
            ImagePayload p; p.modId = modId; p.ok = false;
            std::lock_guard<std::mutex> lk(ModBrowserState::imageQueueMutex);
            ModBrowserState::imageQueue.push_back(std::move(p));
            return;
        }
        const std::string& data = res->body;

        int w = 0, h = 0, ch = 0;
        unsigned char* pix = stbi_load_from_memory(
            (const unsigned char*)data.data(), (int)data.size(),
            &w, &h, &ch, 4);
        if (!pix) {
            ImagePayload p; p.modId = modId; p.ok = false;
            std::lock_guard<std::mutex> lk(ModBrowserState::imageQueueMutex);
            ModBrowserState::imageQueue.push_back(std::move(p));
            return;
        }

        ImagePayload p;
        p.modId = modId;
        p.width = w;
        p.height = h;
        p.pixels.assign(pix, pix + (size_t)w * h * 4);
        p.ok = true;
        stbi_image_free(pix);

        {
            std::lock_guard<std::mutex> lk(ModBrowserState::imageQueueMutex);
            ModBrowserState::imageQueue.push_back(std::move(p));
        }
    } catch (...) {
        ImagePayload p; p.modId = modId; p.ok = false;
        std::lock_guard<std::mutex> lk(ModBrowserState::imageQueueMutex);
        ModBrowserState::imageQueue.push_back(std::move(p));
    }
}

static void FetchAllThumbnailsAsync() {
    ModBrowserState::imagesDownloading = true;
    std::vector<std::pair<int,std::string>> jobs;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        for (auto& m : ModBrowserState::mods) {
            if (m.imgState == ImageState::NotLoaded && !m.thumbUrl.empty()) {
                m.imgState = ImageState::Loading;
                jobs.push_back({m.id, m.thumbUrl});
            }
        }
    }
    for (auto& [id, url] : jobs) DownloadThumbnail(id, url);
    ModBrowserState::imagesDownloading = false;
}

// Processa a fila na main thread (upload pra GPU)
static void ProcessImageQueue() {
    std::deque<ImagePayload> local;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::imageQueueMutex);
        std::swap(local, ModBrowserState::imageQueue);
    }
    if (local.empty()) return;

    std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
    for (auto& p : local) {
        auto it = std::find_if(ModBrowserState::mods.begin(), ModBrowserState::mods.end(),
            [&](const ModEntry& m) { return m.id == p.modId; });
        if (it == ModBrowserState::mods.end()) continue;

        if (!p.ok) { it->imgState = ImageState::Failed; continue; }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p.width, p.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, p.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        it->glTexture = tex;
        it->imgState = ImageState::Loaded;
    }
}

// ============================================================
// Extracao de .otr/.o2r de um zip
// ============================================================

static bool ExtractModFilesFromZip(const std::filesystem::path& zipPath,
                                   const std::filesystem::path& modsPath) {
    int err = 0;
    zip_t* z = zip_open(zipPath.string().c_str(), ZIP_RDONLY, &err);
    if (!z) { SPDLOG_ERROR("[ModBrowser] zip_open falhou ({})", err); return false; }

    int extracted = 0;
    zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_uint64_t i = 0; i < (zip_uint64_t)n; i++) {
        const char* name = zip_get_name(z, i, 0);
        if (!name) continue;

        std::string sname = name;
        std::string lower = sname;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        bool isModFile = false;
        for (const char* ext : { ".otr", ".o2r", ".ootr" }) {
            size_t len = std::strlen(ext);
            if (lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0) {
                isModFile = true; break;
            }
        }
        if (!isModFile) continue;

        auto slash = sname.find_last_of("/\\");
        std::string fname = (slash == std::string::npos) ? sname : sname.substr(slash + 1);
        if (fname.empty()) continue;

        zip_file_t* zf = zip_fopen_index(z, i, 0);
        if (!zf) continue;

        std::filesystem::path outPath = modsPath / fname;
        std::ofstream out(outPath, std::ios::binary);
        if (!out) { zip_fclose(zf); continue; }

        char buf[65536];
        zip_int64_t rd;
        while ((rd = zip_fread(zf, buf, sizeof(buf))) > 0) out.write(buf, rd);
        out.close();
        zip_fclose(zf);
        extracted++;
        SPDLOG_INFO("[ModBrowser] Extraido: {}", fname);
    }
    zip_close(z);
    SPDLOG_INFO("[ModBrowser] Extraidos {} arquivos", extracted);
    return extracted > 0;
}

// ============================================================
// Download de mod
// ============================================================

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
        cli.set_read_timeout(180, 0);
        cli.set_write_timeout(180, 0);
        cli.enable_server_certificate_verification(false);

        std::filesystem::path modsPath = GetModsPath();
        std::filesystem::path dest = modsPath / (SanitizeFilename(modName) + ".zip");
        std::ofstream out(dest, std::ios::binary);
        if (!out) {
            ModBrowserState::lastError = "Nao consegui abrir arquivo";
            Notification::Emit({ .message = "Erro: nao consegui salvar" });
            ModBrowserState::downloading = false; return;
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
            SPDLOG_INFO("[ModBrowser] Baixado {} -> {}", modName, dest.string());
            bool ok = ExtractModFilesFromZip(dest, modsPath);
            if (ok) {
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                Notification::Emit({ .message = "Instalado: " + modName + " (reinicie o jogo)" });
            } else {
                Notification::Emit({ .message = "Baixado (sem .otr): " + modName });
            }
        } else {
            std::string st = res ? std::to_string(res->status) : "sem resposta";
            ModBrowserState::lastError = "Download falhou (HTTP " + st + ")";
            std::error_code ec; std::filesystem::remove(dest, ec);
            Notification::Emit({ .message = "Erro ao baixar " + modName });
        }
    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Excecao: ") + e.what();
        Notification::Emit({ .message = "Erro: " + std::string(e.what()) });
    }
    ModBrowserState::downloading = false;
    ModBrowserState::downloadModId = 0;
}

// ============================================================
// Fetch de detalhes do mod (descricao)
// ============================================================

static void FetchDetailsAsync(int modId) {
    ModBrowserState::detailsFetching = true;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::detailsMutex);
        ModBrowserState::detailsText = "Carregando...";
        ModBrowserState::detailsModId = modId;
    }

    std::string path = "/apiv11/Mod/" + std::to_string(modId) + "/ProfilePage";
    std::string body = HttpGet("gamebanana.com", path);

    std::string text = "(sem descricao)";
    if (!body.empty()) {
        try {
            json j = json::parse(body);
            if (j.contains("_sText")) text = j["_sText"].get<std::string>();
        } catch (...) {}
    }
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::detailsMutex);
        ModBrowserState::detailsText = text;
    }
    ModBrowserState::detailsFetching = false;
}

// ============================================================
// UI
// ============================================================

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
    if (ModBrowserState::downloading) {
        std::string nameCopy;
        { std::lock_guard<std::mutex> dl(ModBrowserState::downloadMutex);
          nameCopy = ModBrowserState::downloadModName; }
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

    ImGui::BeginChild("ModListScroll", ImVec2(0, 450), true);
    for (auto& m : ModBrowserState::mods) {
        if (!filter.empty()) {
            std::string lower = m.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(filter) == std::string::npos) continue;
        }
        ImGui::PushID(m.id);

        // Thumbnail (esquerda) + texto (direita)
        float thumbH = 80.0f;
        if (m.imgState == ImageState::Loaded && m.glTexture != 0) {
            ImGui::Image((ImTextureID)(intptr_t)m.glTexture, ImVec2(thumbH * 1.5f, thumbH));
            ImGui::SameLine();
        } else if (m.imgState == ImageState::Loading) {
            ImGui::BeginChild("##thumb", ImVec2(thumbH * 1.5f, thumbH), true);
            ImGui::TextDisabled("...");
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", m.name.c_str());
        if (!m.author.empty())   ImGui::TextDisabled("por %s", m.author.c_str());
        if (!m.category.empty()) ImGui::TextDisabled("Categoria: %s", m.category.c_str());

        if (ImGui::Button("Abrir pagina")) {
            if (!m.profileUrl.empty()) SDL_OpenURL(m.profileUrl.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Detalhes")) {
            ModBrowserState::selectedModId = m.id;
            std::thread(FetchDetailsAsync, m.id).detach();
        }
        ImGui::SameLine();

        bool downloadingThis = ModBrowserState::downloading &&
                               ModBrowserState::downloadModId == m.id;
        bool anyDownloading = ModBrowserState::downloading;
        ImGui::BeginDisabled(anyDownloading);
        if (ImGui::Button(downloadingThis ? "Baixando..." : "Baixar")) {
            std::thread(DownloadModAsync, m.id, m.name).detach();
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void DrawDetails(WidgetInfo& info) {
    int sel = ModBrowserState::selectedModId;
    if (sel == 0) {
        ImGui::TextDisabled("Nenhum mod selecionado.");
        ImGui::TextWrapped("Clica em 'Detalhes' em algum mod da aba Browse.");
        return;
    }

    std::string name;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        auto it = std::find_if(ModBrowserState::mods.begin(), ModBrowserState::mods.end(),
            [&](const ModEntry& m){ return m.id == sel; });
        if (it != ModBrowserState::mods.end()) name = it->name;
    }
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", name.c_str());
    ImGui::Separator();

    if (ModBrowserState::detailsFetching) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Carregando detalhes...");
        return;
    }

    std::string txt;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::detailsMutex);
        txt = ModBrowserState::detailsText;
    }
    ImGui::BeginChild("DetailsScroll", ImVec2(0, 450), true);
    ImGui::TextWrapped("%s", txt.c_str());
    ImGui::EndChild();
}

// ============================================================
// Registro do menu
// ============================================================

void SohMenu::AddMenuModBrowser() {
    AddMenuEntry("Mod Browser", CVAR_SETTING("Menu.ModBrowserSidebarSection"));
    WidgetPath path;

    // ---- Browse ----
    path = { "Mod Browser", "Browse", SECTION_COLUMN_1 };
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);

    AddWidget(path, "Acoes", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Atualizar lista de mods", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            if (ModBrowserState::fetching) return;
            std::thread(FetchModsAsync).detach();
            Notification::Emit({ .message = "Buscando mods..." });
        });
    AddWidget(path, "Baixar thumbnails", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            if (ModBrowserState::imagesDownloading) return;
            std::thread(FetchAllThumbnailsAsync).detach();
            Notification::Emit({ .message = "Baixando imagens..." });
        });

    AddWidget(path, "Filtrar", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "##ModBrowserFilter", WIDGET_CUSTOM).CustomFunction([](WidgetInfo& info) {
        ImGui::InputTextWithHint("##ModBrowserFilter", "Buscar...",
                                 ModBrowserState::searchBuf, IM_ARRAYSIZE(ModBrowserState::searchBuf));
    });

    AddWidget(path, "Resultados", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "##ModBrowserList", WIDGET_CUSTOM).CustomFunction(DrawModList);

    // ---- Detalhes ----
    path.sidebarName = "Detalhes";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;
    AddWidget(path, "##DetailsView", WIDGET_CUSTOM).CustomFunction(DrawDetails);

    // ---- Sobre ----
    path.sidebarName = "Sobre";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Info", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Mod Browser experimental para SoH.\n"
              "Fonte: GameBanana API v11.\n"
              "Features: fetch + cache + thumbnails + detalhes + download + auto-install.",
              WIDGET_TEXT);
}

// ============================================================
// Init: chamar do SohGui ou de algum lugar de startup
// ============================================================

void ModBrowserInit() {
    if (ModBrowserState::cacheLoaded) return;
    ModBrowserState::cacheLoaded = true;
    std::vector<ModEntry> cached;
    if (LoadCache(cached)) {
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        ModBrowserState::mods = std::move(cached);
    }
}

} // namespace SohGui
