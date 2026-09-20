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

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <fast/Fast3dGui.h>
#include <fast/resource/type/Texture.h>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>

#include <SDL2/SDL.h>
#include <zip.h>

#include <stb_image.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <future>
#include <atomic>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <deque>

namespace SohGui {

using namespace UIWidgets;
using json = nlohmann::json;

enum class ImageState : int {
    NotLoaded = 0,
    Downloading = 1,
    NeedUpload = 2,
    Loaded = 3,
    Failed = 4
};

struct ModEntry {
    int         id = 0;
    std::string name;
    std::string profileUrl;
    std::string downloadUrl;
    std::string author;
    std::string category;
    std::string thumbUrl;
    std::string description;

    ImageState     imgState = ImageState::NotLoaded;
    std::string    thumbDiskPath;
    ImTextureID    texId = nullptr;

    // Detalhes expansiveis inline (substitui a antiga aba "Detalhes" separada)
    bool        detailsExpanded = false;
    bool        detailsFetching = false;
    bool        detailsFetched  = false;
    std::string detailsText;
};

namespace ModBrowserState {
    static std::vector<ModEntry> mods;
    static std::mutex            modsMutex;
    static std::atomic<bool>     fetching{ false };
    static std::atomic<int>      fetchModsSoFar{ 0 };
    static std::string           lastError;
    static char                  searchBuf[128] = "";

    static std::atomic<bool>     downloading{ false };
    static std::atomic<int>      downloadModId{ 0 };
    static std::mutex            downloadMutex;
    static std::string           downloadModName;
    static std::atomic<size_t>   downloadBytes{ 0 };
    static std::atomic<size_t>   downloadTotal{ 0 };

    static std::atomic<bool>     imagesDownloading{ false };
    static std::atomic<int>      thumbsTotal{ 0 };
    static std::atomic<int>      thumbsDone{ 0 };

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

static std::filesystem::path GetThumbsPath() {
    std::filesystem::path p = GetModsPath() / ".modbrowser_thumbs";
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

static std::string ExtensionFromUrl(const std::string& url) {
    auto last = url.find_last_of('.');
    if (last == std::string::npos) return ".png";
    std::string ext = url.substr(last);
    auto q = ext.find_first_of("?#");
    if (q != std::string::npos) ext = ext.substr(0, q);
    if (ext.size() > 5 || ext.size() < 2) return ".png";
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

// Separa "https://host/path?query" em host e path.
static bool ParseUrl(const std::string& url, std::string& host, std::string& path) {
    std::string s = url;
    if (s.rfind("https://", 0) == 0) s = s.substr(8);
    else if (s.rfind("http://", 0) == 0) s = s.substr(7);
    auto slash = s.find('/');
    if (slash == std::string::npos) {
        host = s;
        path = "/";
    } else {
        host = s.substr(0, slash);
        path = s.substr(slash);
    }
    return !host.empty();
}

static std::shared_ptr<Fast::Fast3dGui> GetFast3dGui() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx) return nullptr;
    auto win = ctx->GetWindow();
    if (!win) return nullptr;
    auto gui = win->GetGui();
    if (!gui) return nullptr;
    return std::dynamic_pointer_cast<Fast::Fast3dGui>(gui);
}

// ============================================================
// Cache
// ============================================================

static void SaveCache(const std::vector<ModEntry>& mods) {
    try {
        json j;
        j["timestamp"] = (long long)std::time(nullptr);
        j["mods"] = json::array();
        for (auto& m : mods) {
            j["mods"].push_back({
                {"id", m.id}, {"name", m.name},
                {"profileUrl", m.profileUrl}, {"downloadUrl", m.downloadUrl},
                {"author", m.author}, {"category", m.category},
                {"thumbUrl", m.thumbUrl}
            });
        }
        std::ofstream out(GetCachePath());
        out << j.dump();
        SPDLOG_INFO("[ModBrowser] Cache salvo ({} mods)", mods.size());
    } catch (const std::exception& e) {
        SPDLOG_WARN("[ModBrowser] Falha cache save: {}", e.what());
    }
}

static bool LoadCache(std::vector<ModEntry>& out) {
    try {
        std::ifstream in(GetCachePath());
        if (!in) return false;
        json j; in >> j;
        if (!j.contains("timestamp") || !j.contains("mods")) return false;
        long long ts = j["timestamp"].get<long long>();
        long long now = (long long)std::time(nullptr);
        if (now - ts > 6 * 3600) { SPDLOG_INFO("[ModBrowser] Cache expirado"); return false; }
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
    } catch (...) { return false; }
}

// ============================================================
// HTTP
// ============================================================

static std::string HttpGet(const std::string& host, const std::string& path) {
    try {
        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(20, 0);
        cli.set_write_timeout(20, 0);
        cli.enable_server_certificate_verification(false);
        cli.set_default_headers({
            {"User-Agent", "SohModBrowser/1.0 (+https://github.com/kaoriinacio/Shipwright)"}
        });

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
        if (j.is_object() && j.contains("_aRecords") && j["_aRecords"].is_array()) {
            arr = &j["_aRecords"];
        } else if (j.is_array()) {
            arr = &j;
        } else if (j.is_object()) {
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
// Fetch (paginado em lotes paralelos + dedup) + thumbnails encadeadas
// ============================================================

static void FetchAllThumbnailsAsync(); // fwd decl

static void FetchModsAsync() {
    ModBrowserState::fetching = true;
    ModBrowserState::fetchModsSoFar = 0;
    ModBrowserState::lastError.clear();

    std::vector<ModEntry> allMods;
    std::unordered_set<int> seenIds;

    const int MAX_PAGES  = 100;
    const int BATCH_SIZE = 5; // paginas buscadas em paralelo por lote

    bool done = false;
    for (int batchStart = 1; !done && batchStart <= MAX_PAGES; batchStart += BATCH_SIZE) {
        std::vector<std::future<std::pair<int, std::vector<ModEntry>>>> futures;
        int batchEnd = std::min(batchStart + BATCH_SIZE - 1, MAX_PAGES);

        for (int page = batchStart; page <= batchEnd; ++page) {
            futures.push_back(std::async(std::launch::async, [page]() {
                std::string path = "/apiv11/Game/16121/Subfeed"
                                   "?_nPage=" + std::to_string(page) +
                                   "&_sSort=new"
                                   "&_csvModelInclusions=Mod";
                std::string body = HttpGet("gamebanana.com", path);
                std::vector<ModEntry> mods;
                if (!body.empty()) mods = ParseGameBananaMods(body);
                return std::make_pair(page, mods);
            }));
        }

        std::vector<std::pair<int, std::vector<ModEntry>>> results;
        results.reserve(futures.size());
        for (auto& f : futures) results.push_back(f.get());
        std::sort(results.begin(), results.end(),
                  [](auto& a, auto& b) { return a.first < b.first; });

        // Processa em ordem de pagina pra manter o dedup e o corte deterministicos,
        // mesmo com as requisicoes tendo sido disparadas em paralelo.
        for (auto& [page, mods] : results) {
            if (mods.empty()) {
                SPDLOG_INFO("[ModBrowser] Fim das paginas na {}", page);
                done = true;
                break;
            }

            int novos = 0;
            for (auto& m : mods) {
                if (m.id <= 0) continue;
                if (seenIds.insert(m.id).second) {
                    allMods.push_back(std::move(m));
                    novos++;
                }
            }
            ModBrowserState::fetchModsSoFar = (int)allMods.size();

            SPDLOG_INFO("[ModBrowser] Pagina {} -> {} mods ({} novos, {} total)",
                        page, mods.size(), novos, allMods.size());

            if (novos == 0) {
                SPDLOG_INFO("[ModBrowser] Pagina {} sem novidade, parando", page);
                done = true;
                break;
            }
            if (mods.size() < 15) {
                SPDLOG_INFO("[ModBrowser] Pagina {} parcial ({} itens), ultima pagina", page, mods.size());
                done = true;
                break;
            }
        }

        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    if (!allMods.empty()) {
        {
            std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);
            ModBrowserState::mods = std::move(allMods);
        }
        SaveCache(ModBrowserState::mods);
        SPDLOG_INFO("[ModBrowser] {} mods carregados (total).", ModBrowserState::mods.size());
    } else {
        SPDLOG_ERROR("[ModBrowser] Falha: {}", ModBrowserState::lastError);
    }

    ModBrowserState::fetching = false;

    // "Atualizar lista" agora ja puxa as thumbnails automaticamente, sem precisar
    // clicar em "Baixar thumbnails" depois. Chamada direta (mesma thread de
    // background), FetchAllThumbnailsAsync paraleliza os downloads internamente.
    if (!ModBrowserState::mods.empty()) {
        FetchAllThumbnailsAsync();
    }
}

// ============================================================
// Thumbnails
// ============================================================

static void DownloadThumbnailToDisk(int modId, std::string url) {
    if (url.empty()) return;
    try {
        std::string proto = "https://";
        if (url.rfind(proto, 0) == 0) url = url.substr(proto.size());
        auto slash = url.find('/');
        if (slash == std::string::npos) return;
        std::string host = url.substr(0, slash);
        std::string path = url.substr(slash);

        std::string ext = ExtensionFromUrl(path);

        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(20, 0);
        cli.enable_server_certificate_verification(false);
        cli.set_default_headers({
            {"User-Agent", "SohModBrowser/1.0 (+https://github.com/kaoriinacio/Shipwright)"}
        });

        auto res = cli.Get(path.c_str());
        if (!res || res->status != 200) {
            SPDLOG_WARN("[ModBrowser] Thumb HTTP {} (mod {})",
                        res ? std::to_string(res->status) : "sem resposta", modId);
            std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
            for (auto& m : ModBrowserState::mods) if (m.id == modId) m.imgState = ImageState::Failed;
            return;
        }

        if (ext == ".png" && res->has_header("Content-Type")) {
            std::string ct = res->get_header_value("Content-Type");
            if (ct.find("jpeg") != std::string::npos || ct.find("jpg") != std::string::npos) ext = ".jpg";
            else if (ct.find("png") != std::string::npos) ext = ".png";
        }

        std::filesystem::path diskPath = GetThumbsPath() / (std::to_string(modId) + ext);
        std::ofstream out(diskPath, std::ios::binary);
        out.write(res->body.data(), res->body.size());
        out.close();

        SPDLOG_INFO("[ModBrowser] Thumb salva: {} ({} bytes)", diskPath.string(), res->body.size());

        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        for (auto& m : ModBrowserState::mods) {
            if (m.id == modId) {
                m.thumbDiskPath = diskPath.string();
                m.imgState = ImageState::NeedUpload;
                break;
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("[ModBrowser] Excecao thumb: {}", e.what());
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        for (auto& m : ModBrowserState::mods) if (m.id == modId) m.imgState = ImageState::Failed;
    }
}

// Baixa varias thumbnails ao mesmo tempo (pool de workers) em vez de uma por vez.
// Tambem reconsidera thumbnails que falharam antes (Failed), nao so as nunca
// tentadas (NotLoaded) -- clicar "Baixar thumbnails" agora serve pra tentar de
// novo o que deu erro.
static void FetchAllThumbnailsAsync() {
    ModBrowserState::imagesDownloading = true;

    std::vector<std::pair<int,std::string>> jobs;
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        for (auto& m : ModBrowserState::mods) {
            if ((m.imgState == ImageState::NotLoaded || m.imgState == ImageState::Failed) &&
                !m.thumbUrl.empty()) {
                m.imgState = ImageState::Downloading;
                jobs.push_back({m.id, m.thumbUrl});
            }
        }
    }

    ModBrowserState::thumbsTotal = (int)jobs.size();
    ModBrowserState::thumbsDone = 0;

    if (jobs.empty()) {
        ModBrowserState::imagesDownloading = false;
        return;
    }

    SPDLOG_INFO("[ModBrowser] Baixando {} thumbnails", jobs.size());

    std::atomic<size_t> nextJob{0};
    const int WORKERS = (int)std::min<size_t>(8, jobs.size());
    std::vector<std::thread> workers;
    workers.reserve(WORKERS);
    for (int w = 0; w < WORKERS; ++w) {
        workers.emplace_back([&jobs, &nextJob]() {
            size_t idx;
            while ((idx = nextJob.fetch_add(1)) < jobs.size()) {
                DownloadThumbnailToDisk(jobs[idx].first, jobs[idx].second);
                ModBrowserState::thumbsDone++;
            }
        });
    }
    for (auto& t : workers) t.join();

    ModBrowserState::imagesDownloading = false;
}

// Carrega o arquivo em disco com stb_image e registra via LoadGuiTexture.
static void UploadPendingThumbnails() {
    auto gui = GetFast3dGui();
    if (!gui) return;

    std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
    for (auto& m : ModBrowserState::mods) {
        if (m.imgState != ImageState::NeedUpload || m.thumbDiskPath.empty()) continue;

        std::string texName = "mb_thumb_" + std::to_string(m.id);
        int w = 0, h = 0, ch = 0;
        unsigned char* pix = stbi_load(m.thumbDiskPath.c_str(), &w, &h, &ch, 4);
        if (!pix || w <= 0 || h <= 0) {
            SPDLOG_WARN("[ModBrowser] stbi_load falhou: {}", m.thumbDiskPath);
            if (pix) stbi_image_free(pix);
            m.imgState = ImageState::Failed;
            continue;
        }

        size_t bytes = (size_t)w * h * 4;
        auto buf = std::make_shared<std::vector<char>>(bytes);
        std::memcpy(buf->data(), pix, bytes);
        stbi_image_free(pix);

        Fast::Texture tex;
        tex.Type = Fast::TextureType::RGBA32bpp;
        tex.Width = (uint16_t)w;
        tex.Height = (uint16_t)h;
        tex.ImageDataSize = (uint32_t)bytes;
        tex.ImageData = reinterpret_cast<uint8_t*>(buf->data());
        tex.Flags = 0;
        tex.mImageBuffer = buf;

        ImVec4 tint(1.0f, 1.0f, 1.0f, 1.0f);
        try {
            gui->LoadGuiTexture(texName, tex, "", tint);
            m.texId = gui->GetTextureByName(texName);
            if (m.texId) {
                m.imgState = ImageState::Loaded;
                SPDLOG_INFO("[ModBrowser] Textura carregada: {} ({}x{})", texName, w, h);
            } else {
                m.imgState = ImageState::Failed;
                SPDLOG_WARN("[ModBrowser] GetTextureByName null pra {}", texName);
            }
        } catch (const std::exception& e) {
            m.imgState = ImageState::Failed;
            SPDLOG_WARN("[ModBrowser] LoadGuiTexture excecao: {}", e.what());
        }
    }
}

// ============================================================
// Extracao de zip
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
// Download
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
        std::filesystem::path modsPath = GetModsPath();
        std::string baseName = SanitizeFilename(modName);

        // ---------- Passo 1: pega a lista de arquivos via ProfilePage ----------
        SPDLOG_INFO("[ModBrowser] Buscando ProfilePage do mod {}", modId);
        std::string profBody = HttpGet(
            "gamebanana.com",
            "/apiv11/Mod/" + std::to_string(modId) + "/ProfilePage");

        if (profBody.empty()) {
            ModBrowserState::lastError = "Falha ao pegar ProfilePage";
            Notification::Emit({ .message = "Erro: nao consegui ler info do mod" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        int fileId = 0;
        std::string realFileName;
        std::string realExt;
        std::string dlUrl;   // URL completa do arquivo (do _sDownloadUrl)
        try {
            json prof = json::parse(profBody);
            if (!prof.contains("_aFiles") || !prof["_aFiles"].is_array() ||
                prof["_aFiles"].empty()) {
                ModBrowserState::lastError = "Mod sem arquivos";
                Notification::Emit({ .message = "Mod sem arquivos pra baixar" });
                ModBrowserState::downloading = false;
                ModBrowserState::downloadModId = 0;
                return;
            }

            const json* chosen = nullptr;
            for (auto& f : prof["_aFiles"]) {
                if (!f.is_object()) continue;
                std::string fname = f.value("_sFile", "");
                std::string lower = fname;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                for (const char* e : { ".otr", ".o2r", ".ootr", ".zip" }) {
                    size_t el = std::strlen(e);
                    if (lower.size() >= el &&
                        lower.compare(lower.size() - el, el, e) == 0) {
                        chosen = &f;
                        break;
                    }
                }
                if (chosen) break;
            }
            if (!chosen) {
                ModBrowserState::lastError = "Nenhum arquivo instalavel encontrado (.otr/.o2r/.ootr/.zip)";
                Notification::Emit({ .message = "Mod sem arquivo instalavel reconhecido" });
                ModBrowserState::downloading = false;
                ModBrowserState::downloadModId = 0;
                return;
            }

            fileId       = chosen->value("_idRow", 0);
            realFileName = chosen->value("_sFile", "");
            dlUrl        = chosen->value("_sDownloadUrl", "");
            SPDLOG_INFO("[ModBrowser] Arquivo escolhido: '{}' (id={}) dlUrl='{}'",
                        realFileName, fileId, dlUrl);
        } catch (const std::exception& e) {
            ModBrowserState::lastError = std::string("Parse ProfilePage: ") + e.what();
            Notification::Emit({ .message = "Erro ao ler info do mod" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        // Fallback se a API não mandou _sDownloadUrl
        if (dlUrl.empty() && fileId > 0) {
            dlUrl = "https://gamebanana.com/apiv11/Mod/" +
                    std::to_string(modId) + "/Download/" + std::to_string(fileId);
        }

        if (dlUrl.empty()) {
            ModBrowserState::lastError = "Sem URL de download";
            Notification::Emit({ .message = "Arquivo invalido no mod" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        if (!realFileName.empty()) {
            auto dot = realFileName.find_last_of('.');
            if (dot != std::string::npos) {
                realExt = realFileName.substr(dot);
                std::transform(realExt.begin(), realExt.end(), realExt.begin(), ::tolower);
            }
        }

        // ---------- Passo 2: baixa o arquivo real ----------
        std::string host, path;
        if (!ParseUrl(dlUrl, host, path)) {
            ModBrowserState::lastError = "URL de download invalida";
            Notification::Emit({ .message = "URL invalida do mod" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }
        SPDLOG_INFO("[ModBrowser] Baixando de https://{}{}", host, path);

        httplib::Client cli("https://" + host);
        cli.set_follow_location(true);
        cli.set_connection_timeout(15, 0);
        cli.set_read_timeout(180, 0);
        cli.set_write_timeout(180, 0);
        cli.enable_server_certificate_verification(false);
        cli.set_default_headers({
            {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) SohModBrowser/1.0"},
            {"Accept", "*/*"}
        });

        std::filesystem::path tempPath = modsPath / (baseName + ".download");
        std::ofstream out(tempPath, std::ios::binary);
        if (!out) {
            ModBrowserState::lastError = "Nao consegui abrir arquivo";
            Notification::Emit({ .message = "Erro: nao consegui salvar" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        auto res = cli.Get(path.c_str(),
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

        if (!res) {
            ModBrowserState::lastError = "Sem resposta do servidor";
            std::error_code ec; std::filesystem::remove(tempPath, ec);
            Notification::Emit({ .message = "Erro: sem resposta do servidor" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        SPDLOG_INFO("[ModBrowser] HTTP {} ({} bytes baixados)",
                    res->status, ModBrowserState::downloadBytes.load());

        if (res->status != 200) {
            ModBrowserState::lastError = "Download falhou (HTTP " +
                                         std::to_string(res->status) + ")";
            std::error_code ec; std::filesystem::remove(tempPath, ec);
            Notification::Emit({ .message = "Erro ao baixar " + modName });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        // ---------- Detecção por magic bytes ----------
        char magic[8] = {0};
        { std::ifstream in(tempPath, std::ios::binary); in.read(magic, 8); }
        SPDLOG_INFO("[ModBrowser] Magic: {:02X} {:02X} {:02X} {:02X}  ({})",
                    (unsigned char)magic[0], (unsigned char)magic[1],
                    (unsigned char)magic[2], (unsigned char)magic[3], modName);

        bool isZip = ((unsigned char)magic[0] == 0x50 && (unsigned char)magic[1] == 0x4B);
        bool isMpq = ((unsigned char)magic[0] == 0x4D && (unsigned char)magic[1] == 0x50 &&
                      (unsigned char)magic[2] == 0x51 && (unsigned char)magic[3] == 0x1A);

        std::error_code ec;

        bool isHtmlOrJson = ((unsigned char)magic[0] == 0x20 ||
                             (unsigned char)magic[0] == 0x3C ||
                             (unsigned char)magic[0] == 0x7B);
        if (isHtmlOrJson && !isZip && !isMpq) {
            ModBrowserState::lastError = "Servidor retornou HTML/JSON, nao o arquivo";
            std::filesystem::remove(tempPath, ec);
            Notification::Emit({ .message = "Servidor nao entregou o arquivo (tente de novo)" });
            ModBrowserState::downloading = false;
            ModBrowserState::downloadModId = 0;
            return;
        }

        if (isZip) {
            SPDLOG_INFO("[ModBrowser] ZIP detectado, extraindo...");
            bool ok = ExtractModFilesFromZip(tempPath, modsPath);
            if (ok) {
                std::filesystem::remove(tempPath, ec);
                Notification::Emit({ .message = "Instalado: " + modName + " (reinicie)" });
            } else {
                std::filesystem::path finalZip = modsPath / (baseName + ".zip");
                std::filesystem::rename(tempPath, finalZip, ec);
                Notification::Emit({ .message = "Baixado (sem .otr dentro): " + modName });
            }
        } else if (realExt == ".otr" || realExt == ".o2r" || realExt == ".ootr" || isMpq) {
            std::filesystem::path finalPath = modsPath / (baseName + ".otr");
            std::filesystem::rename(tempPath, finalPath, ec);
            SPDLOG_INFO("[ModBrowser] Salvo: {}", finalPath.string());
            Notification::Emit({ .message = "Instalado: " + modName + " (reinicie)" });
        } else {
            std::string ext = realExt.empty() ? ".bin" : realExt;
            std::filesystem::path finalPath = modsPath / (baseName + ext);
            std::filesystem::rename(tempPath, finalPath, ec);
            SPDLOG_WARN("[ModBrowser] Extensao desconhecida '{}', salvo como {}",
                        ext, finalPath.string());
            Notification::Emit({ .message = "Baixado: " + modName + ext });
        }

    } catch (const std::exception& e) {
        ModBrowserState::lastError = std::string("Excecao: ") + e.what();
        SPDLOG_ERROR("[ModBrowser] Excecao download: {}", e.what());
        Notification::Emit({ .message = "Erro: " + std::string(e.what()) });
    }
    ModBrowserState::downloading = false;
    ModBrowserState::downloadModId = 0;
}

// ============================================================
// Detalhes (agora inline / expansivel, por mod)
// ============================================================

static void FetchModDetailsAsync(int modId) {
    {
        std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
        for (auto& m : ModBrowserState::mods) {
            if (m.id == modId) { m.detailsFetching = true; break; }
        }
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

    std::lock_guard<std::mutex> lk(ModBrowserState::modsMutex);
    for (auto& m : ModBrowserState::mods) {
        if (m.id == modId) {
            m.detailsText     = text;
            m.detailsFetched  = true;
            m.detailsFetching = false;
            break;
        }
    }
}

// ============================================================
// UI
// ============================================================

static void DrawModList(WidgetInfo& info) {
    UploadPendingThumbnails();

    std::lock_guard<std::mutex> lock(ModBrowserState::modsMutex);

    if (ModBrowserState::fetching) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Carregando mods... (%d encontrados ate agora)",
                            ModBrowserState::fetchModsSoFar.load());
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
    if (ModBrowserState::imagesDownloading) {
        int done  = ModBrowserState::thumbsDone.load();
        int total = ModBrowserState::thumbsTotal.load();
        float frac = total > 0 ? (float)done / (float)total : 0.0f;
        char buf[64];
        snprintf(buf, sizeof(buf), "Thumbnails %d/%d", done, total);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), buf);
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

        float thumbH = 80.0f;
        ImVec2 thumbSize(thumbH * 1.5f, thumbH);

        if (m.imgState == ImageState::Loaded && m.texId) {
            ImGui::Image(m.texId, thumbSize);
            ImGui::SameLine();
        } else if (m.imgState == ImageState::Downloading ||
                   m.imgState == ImageState::NeedUpload) {
            ImGui::BeginChild("##thumb", thumbSize, true);
            ImGui::TextDisabled("...");
            ImGui::EndChild();
            ImGui::SameLine();
        } else if (m.imgState == ImageState::Failed) {
            ImGui::BeginChild("##thumbfail", thumbSize, true);
            ImGui::TextDisabled("?");
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", m.name.c_str());
        if (!m.author.empty() || !m.category.empty()) {
            std::string sub;
            if (!m.author.empty())   sub += "por " + m.author;
            if (!m.category.empty()) sub += (sub.empty() ? "" : "  |  ") + m.category;
            ImGui::TextDisabled("%s", sub.c_str());
        }

        if (ImGui::Button("Abrir pagina")) {
            if (!m.profileUrl.empty()) SDL_OpenURL(m.profileUrl.c_str());
        }
        ImGui::SameLine();
        // Botao "Detalhes" agora funciona como accordion: clica pra expandir a
        // caixa com o texto inline, clica de novo pra fechar. So dispara o
        // fetch na primeira vez que abre (fica em cache no proprio ModEntry).
        if (ImGui::Button(m.detailsExpanded ? "Fechar detalhes" : "Detalhes")) {
            m.detailsExpanded = !m.detailsExpanded;
            if (m.detailsExpanded && !m.detailsFetched && !m.detailsFetching) {
                std::thread(FetchModDetailsAsync, m.id).detach();
            }
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

        if (m.detailsExpanded) {
            ImGui::Indent();
            if (m.detailsFetching) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Carregando detalhes...");
            } else {
                ImGui::BeginChild("##inlineDetails", ImVec2(0, 140), true);
                ImGui::TextWrapped("%s", m.detailsText.c_str());
                ImGui::EndChild();
            }
            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ============================================================
// Registro
// ============================================================

void SohMenu::AddMenuModBrowser() {
    AddMenuEntry("Mod Browser", CVAR_SETTING("ModBrowserSidebarSection"));
    WidgetPath path;

    path = { "Mod Browser", "Browse", SECTION_COLUMN_1 };
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);

    AddWidget(path, "Acoes", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Atualizar lista de mods", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            if (ModBrowserState::fetching) return;
            std::thread(FetchModsAsync).detach();
            Notification::Emit({ .message = "Buscando mods..." });
        });
    AddWidget(path, "Baixar/retentar thumbnails", WIDGET_BUTTON)
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

    path.sidebarName = "Sobre";
    AddSidebarEntry("Mod Browser", path.sidebarName, 1);
    path.column = SECTION_COLUMN_1;

    AddWidget(path, "Info", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Mod Browser experimental para SoH.\n"
              "Fonte: GameBanana API v11.\n"
              "Auto-install de .otr/.o2r/zips.\n"
              "Atualizar a lista ja baixa as thumbnails junto.\n"
              "Clique em Detalhes pra expandir a descricao do mod.",
              WIDGET_TEXT);
}

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
