#include "DiscordCVars.h"

#include "soh/cvar_prefixes.h"
#include "soh/ShipInit.hpp"
#include <libultraship/bridge/consolevariablebridge.h>

#define CVAR_DISCORD(name) CVAR_ENHANCEMENT("Discord." name)

namespace DiscordCVars {

void Register() {
    CVarRegisterInteger(CVAR_DISCORD("Enabled"),         1);
    CVarRegisterInteger(CVAR_DISCORD("ShowArea"),        1);
    CVarRegisterInteger(CVAR_DISCORD("ShowRoom"),        0);
    CVarRegisterInteger(CVAR_DISCORD("ShowGameTime"),    1);
    CVarRegisterInteger(CVAR_DISCORD("ShowPlayTime"),    1);
    CVarRegisterInteger(CVAR_DISCORD("ShowHealth"),      1);
    CVarRegisterInteger(CVAR_DISCORD("ShowAge"),         1);
    CVarRegisterInteger(CVAR_DISCORD("ShowEquipment"),   0);
    CVarRegisterInteger(CVAR_DISCORD("ShowProgress"),    1);
    CVarRegisterInteger(CVAR_DISCORD("ShowRupees"),      0);
    CVarRegisterInteger(CVAR_DISCORD("UpdateInterval"), 30);
}

} // namespace DiscordCVars

static RegisterShipInitFunc initFunc(DiscordCVars::Register);
