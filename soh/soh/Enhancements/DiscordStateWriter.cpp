#include <cstdio>
#include <cstdint>
#include <ctime>
#include <string>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Discord/DiscordIPC.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#define CVAR_DISCORD(name) CVAR_ENHANCEMENT("Discord." name)

static constexpr const char* DISCORD_CLIENT_ID = "1548247424072417290";
static constexpr const char* LARGE_IMAGE_KEY   = "soh_icon";
static constexpr const char* LARGE_IMAGE_TXT   = "The Legend of Zelda: Ocarina of Time";

// ---------- tabela de cenas (versão EN só, PT-BR vem na etapa 3) ----------
static const char* GetSceneName(int16_t sceneNum) {
    switch (sceneNum) {
        case SCENE_DEKU_TREE: return "Deku Tree";
        case SCENE_DODONGOS_CAVERN: return "Dodongo's Cavern";
        case SCENE_JABU_JABU: return "Jabu Jabu";
        case SCENE_FOREST_TEMPLE: return "Forest Temple";
        case SCENE_FIRE_TEMPLE: return "Fire Temple";
        case SCENE_WATER_TEMPLE: return "Water Temple";
        case SCENE_SPIRIT_TEMPLE: return "Spirit Temple";
        case SCENE_SHADOW_TEMPLE: return "Shadow Temple";
        case SCENE_BOTTOM_OF_THE_WELL: return "Bottom of the Well";
        case SCENE_ICE_CAVERN: return "Ice Cavern";
        case SCENE_GANONS_TOWER: return "Ganon's Tower";
        case SCENE_GERUDO_TRAINING_GROUND: return "Gerudo Training Ground";
        case SCENE_THIEVES_HIDEOUT: return "Thieves' Hideout";
        case SCENE_INSIDE_GANONS_CASTLE: return "Inside Ganon's Castle";
        case SCENE_DEKU_TREE_BOSS: return "Deku Tree Boss";
        case SCENE_DODONGOS_CAVERN_BOSS: return "Dodongo's Cavern Boss";
        case SCENE_JABU_JABU_BOSS: return "Jabu Jabu Boss";
        case SCENE_FOREST_TEMPLE_BOSS: return "Forest Temple Boss";
        case SCENE_FIRE_TEMPLE_BOSS: return "Fire Temple Boss";
        case SCENE_WATER_TEMPLE_BOSS: return "Water Temple Boss";
        case SCENE_SPIRIT_TEMPLE_BOSS: return "Spirit Temple Boss";
        case SCENE_SHADOW_TEMPLE_BOSS: return "Shadow Temple Boss";
        case SCENE_GANONDORF_BOSS: return "Ganondorf Boss";
        case SCENE_GANON_BOSS: return "Ganon Boss";
        case SCENE_HYRULE_FIELD: return "Hyrule Field";
        case SCENE_KAKARIKO_VILLAGE: return "Kakariko Village";
        case SCENE_GRAVEYARD: return "Graveyard";
        case SCENE_ZORAS_RIVER: return "Zora's River";
        case SCENE_KOKIRI_FOREST: return "Kokiri Forest";
        case SCENE_SACRED_FOREST_MEADOW: return "Sacred Forest Meadow";
        case SCENE_LAKE_HYLIA: return "Lake Hylia";
        case SCENE_ZORAS_DOMAIN: return "Zora's Domain";
        case SCENE_ZORAS_FOUNTAIN: return "Zora's Fountain";
        case SCENE_GERUDO_VALLEY: return "Gerudo Valley";
        case SCENE_LOST_WOODS: return "Lost Woods";
        case SCENE_DESERT_COLOSSUS: return "Desert Colossus";
        case SCENE_GERUDOS_FORTRESS: return "Gerudo's Fortress";
        case SCENE_HAUNTED_WASTELAND: return "Haunted Wasteland";
        case SCENE_HYRULE_CASTLE: return "Hyrule Castle";
        case SCENE_DEATH_MOUNTAIN_TRAIL: return "Death Mountain Trail";
        case SCENE_DEATH_MOUNTAIN_CRATER: return "Death Mountain Crater";
        case SCENE_GORON_CITY: return "Goron City";
        case SCENE_LON_LON_RANCH: return "Lon Lon Ranch";
        case SCENE_TEMPLE_OF_TIME: return "Temple of Time";
        case SCENE_CHAMBER_OF_THE_SAGES: return "Chamber of the Sages";
        case SCENE_FISHING_POND: return "Fishing Pond";
        default: return "Unknown Area";
    }
}

// ---------- formatação ----------
static std::string FormatSessionTime() {
    static const time_t start = std::time(nullptr);
    long secs = (long)(std::time(nullptr) - start);
    long h = secs / 3600;
    long m = (secs % 3600) / 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%ldh %02ldmin", h, m);
    else       std::snprintf(buf, sizeof(buf), "%ldmin", m);
    return std::string(buf);
}

static std::string FormatGameTime() {
    uint32_t t = gSaveContext.dayTime;
    int totalMin = (int)((uint64_t)t * 24 * 60 / 0x10000);
    int h = totalMin / 60;
    int m = totalMin % 60;
    const char* period;
    if (h >= 6 && h < 18)       period = "Day";
    else if (h >= 18 && h < 21) period = "Dusk";
    else                        period = "Night";
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %02d:%02d", period, h, m);
    return std::string(buf);
}

static const char* SwordName() {
    switch (gSaveContext.equips.sword) {
        case 0: return "None";
        case 1: return "Kokiri Sword";
        case 2: return "Master Sword";
        case 3: return "Biggoron's Sword";
        default: return "?";
    }
}

static const char* BootsName() {
    switch (gSaveContext.equips.boots) {
        case 0: return "None";
        case 1: return "Kokiri Boots";
        case 2: return "Iron Boots";
        case 3: return "Hover Boots";
        default: return "?";
    }
}

static void CountQuest(int& medallions, int& stones) {
    uint32_t q = gSaveContext.inventory.questItems;
    medallions = 0;
    for (int i = 0; i < 6; i++) if (q & (1 << i)) medallions++;
    stones = 0;
    for (int i = 18; i <= 20; i++) if (q & (1 << i)) stones++;
}

// ---------- payload ----------
static void BuildPresence(std::string& outDetails, std::string& outState) {
    if (gPlayState == nullptr) {
        outDetails = "In menus";
        outState = "";
        return;
    }

    std::string details;
    if (CVarGetInteger(CVAR_DISCORD("ShowArea"), 1)) {
        const char* scene = GetSceneName(gPlayState->sceneNum);
        if (CVarGetInteger(CVAR_DISCORD("ShowRoom"), 0) &&
            gPlayState->roomCtx.curRoom.num >= 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Exploring: %s - Room %d",
                scene, gPlayState->roomCtx.curRoom.num);
            details = buf;
        } else {
            details = std::string("Exploring: ") + scene;
        }
    } else {
        details = "Playing Ocarina of Time";
    }
    outDetails = details;

    std::string state;
    auto add = [&](const std::string& s) {
        if (s.empty()) return;
        if (!state.empty()) state += " • ";
        state += s;
    };

    if (CVarGetInteger(CVAR_DISCORD("ShowAge"), 1)) {
        add("Adult");
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowHealth"), 1)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d hearts", gSaveContext.health / 16);
        add(buf);
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowRupees"), 0)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d rupees", gSaveContext.rupees);
        add(buf);
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowProgress"), 1)) {
        int med, stones;
        CountQuest(med, stones);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Medals %d/6, Stones %d/3", med, stones);
        add(buf);
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowEquipment"), 0)) {
        add(SwordName());
        add(BootsName());
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowGameTime"), 1)) {
        add(FormatGameTime());
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowPlayTime"), 1)) {
        add(FormatSessionTime());
    }

    outState = state;
}

// ---------- loop ----------
static uint32_t sFrameCounter = 0;
static bool s_discordInitialized = false;

static void UpdateDiscordPresence() {
    if (!CVarGetInteger(CVAR_DISCORD("Enabled"), 1)) {
        if (s_discordInitialized) {
            DiscordIPC::Shutdown();
            s_discordInitialized = false;
        }
        return;
    }

    if (!s_discordInitialized) {
        DiscordIPC::Init(DISCORD_CLIENT_ID);
        s_discordInitialized = true;
    }
    DiscordIPC::Tick();

    std::string details, state;
    BuildPresence(details, state);
    DiscordIPC::Update(details, state, LARGE_IMAGE_KEY, LARGE_IMAGE_TXT);
}

void RegisterDiscordStateWriter() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        sFrameCounter++;
        int interval = CVarGetInteger(CVAR_DISCORD("UpdateInterval"), 30);
        if (interval < 10) interval = 10;
        if (sFrameCounter >= (uint32_t)interval) {
            sFrameCounter = 0;
            UpdateDiscordPresence();
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterDiscordStateWriter);
