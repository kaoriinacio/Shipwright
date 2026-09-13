#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace PlayerStats {

struct Data {
    uint64_t totalSeconds     = 0;
    uint32_t deaths           = 0;
    uint32_t rupeesEarned     = 0;
    uint32_t rupeesSpent      = 0;
    uint32_t bestStreakSecs   = 0;
    time_t   firstPlayed      = 0;
    time_t   lastPlayed       = 0;
};

// Variáveis globais do namespace (definidas em PlayerStats.cpp)
extern Data     g_data;
extern time_t   g_sessionStart;
extern uint32_t g_lastRupees;
extern bool     g_wasDead;
extern time_t   g_streakStart;
extern time_t   g_lastAutosave;
extern bool     g_initialized;
extern bool     g_captureRupees;

Data& Get();
void Init();
void Save();
void Reset();

uint64_t TotalSecondsLive();

std::string FormatDuration(uint64_t seconds);
std::string FormatDate(time_t t);

} // namespace PlayerStats