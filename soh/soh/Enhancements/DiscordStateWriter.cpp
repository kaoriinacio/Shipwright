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
static constexpr const char* LARGE_IMAGE_KEY   = "";
static constexpr const char* LARGE_IMAGE_TXT   = "The Legend of Zelda: Ocarina of Time";

// ---------- i18n da presence ----------
static int PresenceLang() {
    return CVarGetInteger(CVAR_DISCORD("Language"), 0);
}
static const char* PickLang(const char* en, const char* ptbr) {
    return (PresenceLang() == 1) ? ptbr : en;
}

// ---------- tabela de cenas traduzida ----------
static const char* GetSceneName(int16_t sceneNum) {
    switch (sceneNum) {
        // Dungeons
        case SCENE_DEKU_TREE: return PickLang("Deku Tree", "Arvore Deku");
        case SCENE_DODONGOS_CAVERN: return PickLang("Dodongo's Cavern", "Caverna Dodongo");
        case SCENE_JABU_JABU: return PickLang("Jabu Jabu", "Barriga do Jabu-Jabu");
        case SCENE_FOREST_TEMPLE: return PickLang("Forest Temple", "Templo da Floresta");
        case SCENE_FIRE_TEMPLE: return PickLang("Fire Temple", "Templo do Fogo");
        case SCENE_WATER_TEMPLE: return PickLang("Water Temple", "Templo da Agua");
        case SCENE_SPIRIT_TEMPLE: return PickLang("Spirit Temple", "Templo do Espirito");
        case SCENE_SHADOW_TEMPLE: return PickLang("Shadow Temple", "Templo das Sombras");
        case SCENE_BOTTOM_OF_THE_WELL: return PickLang("Bottom of the Well", "Fundo do Poco");
        case SCENE_ICE_CAVERN: return PickLang("Ice Cavern", "Caverna de Gelo");
        case SCENE_GANONS_TOWER: return PickLang("Ganon's Tower", "Torre de Ganon");
        case SCENE_GERUDO_TRAINING_GROUND: return PickLang("Gerudo Training Ground", "Campo de Treinamento Gerudo");
        case SCENE_THIEVES_HIDEOUT: return PickLang("Thieves' Hideout", "Esconderijo dos Ladroes");
        case SCENE_INSIDE_GANONS_CASTLE: return PickLang("Inside Ganon's Castle", "Castelo de Ganon (Interior)");
        // Boss rooms
        case SCENE_DEKU_TREE_BOSS: return PickLang("Deku Tree Boss", "Chefe da Arvore Deku");
        case SCENE_DODONGOS_CAVERN_BOSS: return PickLang("Dodongo's Cavern Boss", "Chefe da Caverna Dodongo");
        case SCENE_JABU_JABU_BOSS: return PickLang("Jabu Jabu Boss", "Chefe do Jabu-Jabu");
        case SCENE_FOREST_TEMPLE_BOSS: return PickLang("Forest Temple Boss", "Chefe do Templo da Floresta");
        case SCENE_FIRE_TEMPLE_BOSS: return PickLang("Fire Temple Boss", "Chefe do Templo do Fogo");
        case SCENE_WATER_TEMPLE_BOSS: return PickLang("Water Temple Boss", "Chefe do Templo da Agua");
        case SCENE_SPIRIT_TEMPLE_BOSS: return PickLang("Spirit Temple Boss", "Chefe do Templo do Espirito");
        case SCENE_SHADOW_TEMPLE_BOSS: return PickLang("Shadow Temple Boss", "Chefe do Templo das Sombras");
        case SCENE_GANONDORF_BOSS: return PickLang("Ganondorf Boss", "Chefe Ganondorf");
        case SCENE_GANON_BOSS: return PickLang("Ganon Boss", "Chefe Ganon");
        // Overworld
        case SCENE_HYRULE_FIELD: return PickLang("Hyrule Field", "Campo de Hyrule");
        case SCENE_KAKARIKO_VILLAGE: return PickLang("Kakariko Village", "Vila Kakariko");
        case SCENE_GRAVEYARD: return PickLang("Graveyard", "Cemiterio");
        case SCENE_ZORAS_RIVER: return PickLang("Zora's River", "Rio Zora");
        case SCENE_KOKIRI_FOREST: return PickLang("Kokiri Forest", "Floresta Kokiri");
        case SCENE_SACRED_FOREST_MEADOW: return PickLang("Sacred Forest Meadow", "Clareira Sagrada");
        case SCENE_LAKE_HYLIA: return PickLang("Lake Hylia", "Lago Hylia");
        case SCENE_ZORAS_DOMAIN: return PickLang("Zora's Domain", "Dominio Zora");
        case SCENE_ZORAS_FOUNTAIN: return PickLang("Zora's Fountain", "Fonte Zora");
        case SCENE_GERUDO_VALLEY: return PickLang("Gerudo Valley", "Vale Gerudo");
        case SCENE_LOST_WOODS: return PickLang("Lost Woods", "Bosque Perdido");
        case SCENE_DESERT_COLOSSUS: return PickLang("Desert Colossus", "Colosso do Deserto");
        case SCENE_GERUDOS_FORTRESS: return PickLang("Gerudo's Fortress", "Fortaleza Gerudo");
        case SCENE_HAUNTED_WASTELAND: return PickLang("Haunted Wasteland", "Deserto Assombrado");
        case SCENE_HYRULE_CASTLE: return PickLang("Hyrule Castle", "Castelo de Hyrule");
        case SCENE_DEATH_MOUNTAIN_TRAIL: return PickLang("Death Mountain Trail", "Trilha da Montanha da Morte");
        case SCENE_DEATH_MOUNTAIN_CRATER: return PickLang("Death Mountain Crater", "Cratera da Montanha da Morte");
        case SCENE_GORON_CITY: return PickLang("Goron City", "Cidade Goron");
        case SCENE_LON_LON_RANCH: return PickLang("Lon Lon Ranch", "Rancho Lon Lon");
        case SCENE_TEMPLE_OF_TIME: return PickLang("Temple of Time", "Templo do Tempo");
        case SCENE_CHAMBER_OF_THE_SAGES: return PickLang("Chamber of the Sages", "Camara dos Sabios");
        case SCENE_FISHING_POND: return PickLang("Fishing Pond", "Lago de Pesca");
        default: return PickLang("Unknown Area", "Area Desconhecida");
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
    if (h >= 6 && h < 18)       period = PickLang("Day", "Dia");
    else if (h >= 18 && h < 21) period = PickLang("Dusk", "Anoitecer");
    else                        period = PickLang("Night", "Noite");
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %02d:%02d", period, h, m);
    return std::string(buf);
}

static const char* SwordName() {
    switch ((int)CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD)) {
        case 0: return PickLang("None", "Nenhuma");
        case 1: return "Kokiri Sword";
        case 2: return "Master Sword";
        case 3: return "Biggoron's Sword";
        default: return "?";
    }
}

static const char* BootsName() {
    switch ((int)CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS)) {
        case 0: return PickLang("None", "Nenhuma");
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
        outDetails = PickLang("In menus", "Nos menus");
        outState = "";
        return;
    }

    std::string details;
    if (CVarGetInteger(CVAR_DISCORD("ShowArea"), 1)) {
        const char* scene = GetSceneName(gPlayState->sceneNum);
        if (CVarGetInteger(CVAR_DISCORD("ShowRoom"), 0) &&
            gPlayState->roomCtx.curRoom.num >= 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s: %s - %s %d",
                PickLang("Exploring", "Explorando"), scene,
                PickLang("Room", "Sala"), gPlayState->roomCtx.curRoom.num);
            details = buf;
        } else {
            details = std::string(PickLang("Exploring: ", "Explorando: ")) + scene;
        }
    } else {
        details = PickLang("Playing Ocarina of Time", "Jogando Ocarina of Time");
    }
    outDetails = details;

    std::string state;
    auto add = [&](const std::string& s) {
        if (s.empty()) return;
        if (!state.empty()) state += " • ";
        state += s;
    };

    if (CVarGetInteger(CVAR_DISCORD("ShowAge"), 1)) {
        add(PickLang("Adult", "Adulto"));
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowHealth"), 1)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), PickLang("%d hearts", "%d coracoes"), gSaveContext.health / 16);
        add(buf);
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowRupees"), 0)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), PickLang("%d rupees", "%d rupees"), gSaveContext.rupees);
        add(buf);
    }
    if (CVarGetInteger(CVAR_DISCORD("ShowProgress"), 1)) {
        int med, stones;
        CountQuest(med, stones);
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            PickLang("Medals %d/6, Stones %d/3", "Medalhoes %d/6, Pedras %d/3"),
            med, stones);
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
