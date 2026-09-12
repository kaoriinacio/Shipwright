#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Discord/DiscordIPC.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

static constexpr const char* DISCORD_CLIENT_ID = "1548247424072417290";
static constexpr const char* LARGE_IMAGE_KEY   = "soh_icon"; // "" se não subiu o asset
static constexpr const char* LARGE_IMAGE_TXT   = "The Legend of Zelda: Ocarina of Time";

static const char* GetSceneName(int16_t sceneNum) {
    switch (sceneNum) {
        // Dungeons
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
        case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR: return "Ganon's Tower Collapse Interior";
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE: return "Inside Ganon's Castle Collapse";
        // Boss rooms
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
        case SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR: return "Ganon's Tower Collapse Exterior";
        // Overworld
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
        case SCENE_OUTSIDE_GANONS_CASTLE: return "Outside Ganon's Castle";
        // Mercado / Kakariko / casas
        case SCENE_MARKET_ENTRANCE_DAY: return "Market Entrance (Dia)";
        case SCENE_MARKET_ENTRANCE_NIGHT: return "Market Entrance (Noite)";
        case SCENE_MARKET_ENTRANCE_RUINS: return "Market Entrance (Ruínas)";
        case SCENE_BACK_ALLEY_DAY: return "Back Alley (Dia)";
        case SCENE_BACK_ALLEY_NIGHT: return "Back Alley (Noite)";
        case SCENE_MARKET_DAY: return "Market (Dia)";
        case SCENE_MARKET_NIGHT: return "Market (Noite)";
        case SCENE_MARKET_RUINS: return "Market (Ruínas)";
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY: return "Temple of Time Exterior (Dia)";
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT: return "Temple of Time Exterior (Noite)";
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS: return "Temple of Time Exterior (Ruínas)";
        case SCENE_KNOW_IT_ALL_BROS_HOUSE: return "Know-It-All Bros. House";
        case SCENE_TWINS_HOUSE: return "Twin's House";
        case SCENE_MIDOS_HOUSE: return "Mido's House";
        case SCENE_SARIAS_HOUSE: return "Saria's House";
        case SCENE_KAKARIKO_CENTER_GUEST_HOUSE: return "Kakariko Center Guest House";
        case SCENE_BACK_ALLEY_HOUSE: return "Back Alley House";
        case SCENE_BAZAAR: return "Bazaar";
        case SCENE_KOKIRI_SHOP: return "Kokiri Shop";
        case SCENE_GORON_SHOP: return "Goron Shop";
        case SCENE_ZORA_SHOP: return "Zora Shop";
        case SCENE_POTION_SHOP_KAKARIKO: return "Kakariko Potion Shop";
        case SCENE_POTION_SHOP_MARKET: return "Market Potion Shop";
        case SCENE_POTION_SHOP_GRANNY: return "Granny's Potion Shop";
        case SCENE_BOMBCHU_SHOP: return "Bombchu Shop";
        case SCENE_HAPPY_MASK_SHOP: return "Happy Mask Shop";
        case SCENE_LINKS_HOUSE: return "Link's House";
        case SCENE_DOG_LADY_HOUSE: return "Dog Lady's House";
        case SCENE_STABLE: return "Stable";
        case SCENE_IMPAS_HOUSE: return "Impa's House";
        case SCENE_LAKESIDE_LABORATORY: return "Lake Hylia Laboratory";
        case SCENE_CARPENTERS_TENT: return "Carpenter's Tent";
        case SCENE_GRAVEKEEPERS_HUT: return "Gravekeeper's Hut";
        case SCENE_TREASURE_BOX_SHOP: return "Treasure Box Shop";
        case SCENE_HOUSE_OF_SKULLTULA: return "House of Skulltula";
        case SCENE_LON_LON_BUILDINGS: return "Lon Lon Buildings";
        case SCENE_MARKET_GUARD_HOUSE: return "Market Guard House";
        // Fontes das fadas / covas / templo
        case SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC: return "Great Fairy's Fountain (Magic)";
        case SCENE_FAIRYS_FOUNTAIN: return "Fairy's Fountain";
        case SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS: return "Great Fairy's Fountain (Spells)";
        case SCENE_GROTTOS: return "Grottos";
        case SCENE_REDEAD_GRAVE: return "Redead Grave";
        case SCENE_GRAVE_WITH_FAIRYS_FOUNTAIN: return "Grave with Fairy's Fountain";
        case SCENE_ROYAL_FAMILYS_TOMB: return "Royal Family's Tomb";
        case SCENE_SHOOTING_GALLERY: return "Shooting Gallery";
        case SCENE_TEMPLE_OF_TIME: return "Temple of Time";
        case SCENE_CHAMBER_OF_THE_SAGES: return "Chamber of the Sages";
        case SCENE_WINDMILL_AND_DAMPES_GRAVE: return "Windmill and Dampé's Grave";
        case SCENE_FISHING_POND: return "Fishing Pond";
        case SCENE_BOMBCHU_BOWLING_ALLEY: return "Bombchu Bowling Alley";
        case SCENE_CASTLE_COURTYARD_GUARDS_DAY: return "Castle Courtyard Guards (Dia)";
        case SCENE_CASTLE_COURTYARD_GUARDS_NIGHT: return "Castle Courtyard Guards (Noite)";
        case SCENE_CASTLE_COURTYARD_ZELDA: return "Castle Courtyard Zelda";
        case SCENE_CUTSCENE_MAP: return "Cutscene Map";
        default: return "Área desconhecida";
    }
}

static uint32_t sFrameCounter = 0;
static const uint32_t UPDATE_INTERVAL_FRAMES = 30; // ~1x/seg a 30fps
static bool s_discordInitialized = false;
static const char* s_lastScene = nullptr;
static int s_lastHearts = -1;
static int s_lastAge = -1;

static void UpdateDiscordPresence() {
    if (!s_discordInitialized) {
        DiscordIPC::Init(DISCORD_CLIENT_ID);
        s_discordInitialized = true;
    }

    // ping a cada 15s (o Discord derruba se ficar muito tempo sem ping)
    DiscordIPC::Tick();

    if (gPlayState == nullptr) return;

    int sceneNum = gPlayState->sceneNum;
    const char* sceneName = GetSceneName(sceneNum);
    int hearts = gSaveContext.health / 16;
    int age = (int)gSaveContext.linkAge;

    // só manda update se algo mudou (evita rate limit)
    if (sceneName == s_lastScene && hearts == s_lastHearts && age == s_lastAge) return;
    s_lastScene = sceneName;
    s_lastHearts = hearts;
    s_lastAge = age;

    std::string details = std::string("Explorando: ") + sceneName;
    std::string stateStr = std::string(age == LINK_AGE_CHILD ? "Link Crianca" : "Link Adulto")
                         + " • " + std::to_string(hearts) + " coracoes";

    DiscordIPC::Update(details, stateStr, LARGE_IMAGE_KEY, LARGE_IMAGE_TXT);
}

void RegisterDiscordStateWriter() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        sFrameCounter++;
        if (sFrameCounter >= UPDATE_INTERVAL_FRAMES) {
            sFrameCounter = 0;
            UpdateDiscordPresence();
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterDiscordStateWriter);
