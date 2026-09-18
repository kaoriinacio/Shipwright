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

    // Age se qualquer uma das duas flags está ativa.
    // ENHORSE_OBSTACLE é limpa todo frame; FORCE_REVERSING é a que persiste e trava.
    if (!(horse->stateFlags & (ENHORSE_OBSTACLE | ENHORSE_FORCE_REVERSING))) {
        return;
    }

    f32 sinY = Math_SinS(actor->shape.rot.y);
    f32 cosY = Math_CosS(actor->shape.rot.y);

    // Testa várias distâncias à frente. Tronco de árvore é grosso, então
    // 40 unidades não chega do outro lado. Vai até 130.
    const f32 distances[] = { 40.0f, 70.0f, 100.0f, 130.0f };

    for (f32 dist : distances) {
        Vec3f ahead = actor->world.pos;
        ahead.x += dist * sinY;
        ahead.z += dist * cosY;
        ahead.y = actor->world.pos.y + 60.0f; // começa de cima, como o jogo faz

        CollisionPoly* poly = nullptr;
        s32 bgId = BG_ACTOR_MAX;
        f32 floorY = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &poly, &bgId, actor, &ahead);

        if (floorY <= BGCHECK_Y_MIN || poly == nullptr) {
            continue;
        }

        // Chão andável na mesma altura? É obstáculo fino, libera.
        if (!SurfaceType_IsHorseBlocked(&play->colCtx, poly, bgId) &&
            fabsf(floorY - actor->world.pos.y) < 40.0f) {
            horse->stateFlags &= ~ENHORSE_OBSTACLE;
            horse->stateFlags &= ~ENHORSE_FORCE_REVERSING;
            return;
        }
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
