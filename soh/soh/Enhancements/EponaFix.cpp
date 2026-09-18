#include "EponaFix.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "z64.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"
extern PlayState* gPlayState;
}

static void EponaObstacleFix(Actor* actor, PlayState* play) {
    if (actor->id != ACTOR_EN_HORSE) {
        return;
    }

    EnHorse* horse = (EnHorse*)actor;

    if (horse->action < ENHORSE_ACT_MOUNTED_IDLE ||
        horse->action > ENHORSE_ACT_MOUNTED_GALLOP) {
        return;
    }

    if (!(horse->stateFlags & ENHORSE_OBSTACLE)) {
        return;
    }

    Vec3f ahead = actor->world.pos;
    ahead.z += 40.0f * Math_SinS(actor->shape.rot.y);
    ahead.x += 40.0f * Math_CosS(actor->shape.rot.y);

    CollisionPoly* poly = nullptr;
    s32 bgId = BG_ACTOR_MAX;
    f32 floorY = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &poly, &bgId, actor, &ahead);

    if (floorY <= BGCHECK_Y_MIN || poly == nullptr) {
        return;
    }

    if (!SurfaceType_IsHorseBlocked(&play->colCtx, poly, bgId)) {
        horse->stateFlags &= ~ENHORSE_OBSTACLE;
    }
}

void RegisterEponaFix() {
    COND_HOOK(OnActorUpdate, true, [](void* actor) {
        if (!CVarGetInteger("gEnhancements.Fixes.EponaObstacleFix", 0)) {
            return;
        }
        if (gPlayState == nullptr) {
            return;
        }
        EponaObstacleFix((Actor*)actor, gPlayState);
    });
}

static RegisterShipInitFunc initFunc(RegisterEponaFix, { "gEnhancements.Fixes.EponaObstacleFix" });
