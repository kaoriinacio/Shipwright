#include "EponaFix.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "z64.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"
}

static void EponaObstacleFix(Actor* actor, PlayState* play) {
    if (actor->id != ACTOR_EN_HORSE) {
        return;
    }

    EnHorse* horse = (EnHorse*)actor;

    // Só age quando o Link está montado e a Epona está em movimento
    if (horse->action < ENHORSE_ACT_MOUNTED_IDLE ||
        horse->action > ENHORSE_ACT_MOUNTED_GALLOP) {
        return;
    }

    // Se não está travada, nada a fazer
    if (!(horse->stateFlags & ENHORSE_OBSTACLE)) {
        return;
    }

    // Projeta um ponto à frente da Epona
    Vec3f ahead = actor->world.pos;
    ahead.z += 40.0f * Math_SinS(actor->shape.rot.y);
    ahead.x += 40.0f * Math_CosS(actor->shape.rot.y);

    CollisionPoly* poly = nullptr;
    s32 bgId = BG_ACTOR_MAX;
    f32 floorY = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &poly, &bgId, actor, &ahead);

    // Se não achou chão à frente, deixa travada (é penhasco de verdade)
    if (floorY <= BGCHECK_Y_MIN || poly == nullptr) {
        return;
    }

    // Se a superfície à frente NÃO bloqueia cavalo, é ondulação/beirada de ponte.
    // Libera a Epona.
    if (!SurfaceType_IsHorseBlocked(&play->colCtx, poly, bgId)) {
        horse->stateFlags &= ~ENHORSE_OBSTACLE;
    }
}

void RegisterEponaFix() {
    COND_HOOK(OnActorUpdate, true, [](Actor* actor, PlayState* play) {
        if (!CVarGetInteger("gEnhancements.Fixes.EponaObstacleFix", 0)) {
            return;
        }
        EponaObstacleFix(actor, play);
    });
}