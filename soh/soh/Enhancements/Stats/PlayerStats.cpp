#include "soh/Enhancements/Stats/PlayerStats.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

#include <nlohmann/json.hpp>
#include <libultraship/bridge/consolevariablebridge.h>

#include <fstream>
#include <cstdio>
#include <ctime>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#ifdef _WIN32
#include <windows.h>
#endif

#define CVAR_STATS(name) CVAR_ENHANCEMENT("Stats." name)

namespace PlayerStats {

Data     g_data;
time_t   g_sessionStart    = 0;
uint32_t g_lastRupees      = 0;
bool     g_wasDead         = false;
time_t   g_streakStart     = 0;
time_t   g_lastAutosave    = 0;
bool     g_initialized     = false;
bool     g_captureRupees   = false;

static std::string GetStatsPath() {
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path(exePath);
    size_t lastSlash = path.find_last_of("\\/");
    std::string dir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : ".";
    return dir + "\\soh_stats.json";
#else
    return "./soh_stats.json";
#endif
}

static void LoadFromFile() {
    std::ifstream f(GetStatsPath());
    if (!f.is_open()) return;
    try {
        nlohmann::json j;
        f >> j;
        g_data.totalSeconds   = j.value("totalSeconds",   (uint64_t)0);
        g_data.deaths         = j.value("deaths",         (uint32_t)0);
        g_data.rupeesEarned   = j.value("rupeesEarned",   (uint32_t)0);
        g_data.rupeesSpent    = j.value("rupeesSpent",    (uint32_t)0);
        g_data.bestStreakSecs = j.value("bestStreakSecs", (uint32_t)0);
        g_data.firstPlayed    = (time_t)j.value("firstPlayed", (int64_t)0);
        g_data.lastPlayed     = (time_t)j.value("lastPlayed",  (int64_t)0);
    } catch (...) {
        // arquivo corrompido, ignora
    }
}

static void SaveToFile() {
    time_t now = std::time(nullptr);
    if (g_sessionStart > 0) {
        g_data.totalSeconds += (uint64_t)(now - g_sessionStart);
        g_sessionStart = now;
    }
    g_data.lastPlayed = now;

    nlohmann::json j;
    j["totalSeconds"]   = g_data.totalSeconds;
    j["deaths"]         = g_data.deaths;
    j["rupeesEarned"]   = g_data.rupeesEarned;
    j["rupeesSpent"]    = g_data.rupeesSpent;
    j["bestStreakSecs"] = g_data.bestStreakSecs;
    j["firstPlayed"]    = (int64_t)g_data.firstPlayed;
    j["lastPlayed"]     = (int64_t)g_data.lastPlayed;

    std::ofstream f(GetStatsPath());
    if (f.is_open()) f << j.dump(2);
}

// ---------- API ----------

Data& Get() { return g_data; }

uint64_t TotalSecondsLive() {
    return g_data.totalSeconds + (uint64_t)(std::time(nullptr) - g_sessionStart);
}

std::string FormatDuration(uint64_t seconds) {
    uint64_t h = seconds / 3600;
    uint64_t m = (seconds % 3600) / 60;
    char buf[64];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%lluh %02llumin",
                             (unsigned long long)h, (unsigned long long)m);
    else       std::snprintf(buf, sizeof(buf), "%llumin",
                             (unsigned long long)m);
    return std::string(buf);
}

std::string FormatDate(time_t t) {
    if (t == 0) return "-";
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
                  tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
    return std::string(buf);
}

void Reset() {
    g_data = Data{};
    g_data.firstPlayed = std::time(nullptr);
    g_sessionStart = std::time(nullptr);
    g_streakStart  = g_sessionStart;
    SaveToFile();
}

void Init() {
    if (g_initialized) return;

    LoadFromFile();

    time_t now = std::time(nullptr);
    g_sessionStart = now;
    g_streakStart  = now;
    if (g_data.firstPlayed == 0) g_data.firstPlayed = now;
    g_data.lastPlayed = now;

    g_initialized = true;
}

void Save() {
    if (!g_initialized) return;
    SaveToFile();
}

} // namespace PlayerStats

// ---------- Hooks ----------

static void UpdateProxyCVars() {
    using namespace PlayerStats;
    CVarSetInteger(CVAR_STATS("TotalSeconds"), (int)TotalSecondsLive());
    CVarSetInteger(CVAR_STATS("Deaths"),       (int)Get().deaths);
    CVarSetInteger(CVAR_STATS("RupeesEarned"), (int)Get().rupeesEarned);
    CVarSetInteger(CVAR_STATS("RupeesSpent"),  (int)Get().rupeesSpent);
    CVarSetInteger(CVAR_STATS("BestStreak"),   (int)Get().bestStreakSecs);
}

static void PlayerStats_OnFrame() {
    if (!PlayerStats::Get().firstPlayed) PlayerStats::Init();

    auto& d = PlayerStats::Get();

    // Só conta rupees depois de estar num save carregado
    if (gPlayState != nullptr) {
        uint32_t cur = gSaveContext.rupees;
        if (!PlayerStats::g_captureRupees) {
            PlayerStats::g_lastRupees = cur;
            PlayerStats::g_captureRupees = true;
        } else if (cur > PlayerStats::g_lastRupees) {
            d.rupeesEarned += (cur - PlayerStats::g_lastRupees);
            PlayerStats::g_lastRupees = cur;
        } else if (cur < PlayerStats::g_lastRupees) {
            d.rupeesSpent += (PlayerStats::g_lastRupees - cur);
            PlayerStats::g_lastRupees = cur;
        }
    }

    // Mortes
    bool isDead = (gSaveContext.health <= 0);
    if (isDead && !PlayerStats::g_wasDead) {
        d.deaths++;
        time_t streak = std::time(nullptr) - PlayerStats::g_streakStart;
        if ((uint32_t)streak > d.bestStreakSecs) d.bestStreakSecs = (uint32_t)streak;
        PlayerStats::g_streakStart = std::time(nullptr);
        PlayerStats::g_wasDead = true;
    } else if (!isDead && PlayerStats::g_wasDead) {
        PlayerStats::g_wasDead = false;
    }

    // Atualiza CVars proxy (pro menu mostrar)
    UpdateProxyCVars();

    // Autosave JSON a cada 60s
    time_t now = std::time(nullptr);
    if (now - PlayerStats::g_lastAutosave >= 60) {
        PlayerStats::Save();
        PlayerStats::g_lastAutosave = now;
    }
}

void RegisterPlayerStats() {
    // Registra CVars proxy pro menu
    CVarRegisterInteger(CVAR_STATS("TotalSeconds"), 0);
    CVarRegisterInteger(CVAR_STATS("Deaths"),       0);
    CVarRegisterInteger(CVAR_STATS("RupeesEarned"), 0);
    CVarRegisterInteger(CVAR_STATS("RupeesSpent"),  0);
    CVarRegisterInteger(CVAR_STATS("BestStreak"),   0);

    PlayerStats::Init();

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        PlayerStats_OnFrame();
    });
}

static RegisterShipInitFunc initFunc(RegisterPlayerStats);