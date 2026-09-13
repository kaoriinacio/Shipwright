#include "PhotoMode.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/controls/Mouse.h"
#include "soh/ShipInit.hpp"

#include <libultraship/bridge/consolevariablebridge.h>
#include <cmath>

extern "C" {
#include "global.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define CVAR_PHOTOMODE(name) CVAR_ENHANCEMENT("PhotoMode." name)

// ============================================================================
// Helpers locais — substituem funções que são `static` dentro de z_camera.c
// e por isso não podem ser chamadas de fora.
// ============================================================================

static Vec3f* PhotoMode_Vec3fVecSphGeoAdd(Vec3f* dest, Vec3f* a, VecSph* b) {
    Vec3f vecB;
    vecB.x = b->r * Math_CosS(b->pitch) * Math_SinS(b->yaw);
    vecB.y = b->r * Math_SinS(b->pitch);
    vecB.z = b->r * Math_CosS(b->pitch) * Math_CosS(b->yaw);
    dest->x = a->x + vecB.x;
    dest->y = a->y + vecB.y;
    dest->z = a->z + vecB.z;
    return dest;
}

static f32 PhotoMode_LERPCeilF(f32 target, f32 cur, f32 stepScale, f32 minDiff) {
    f32 diff = target - cur;
    if (fabsf(diff) >= minDiff) {
        return cur + diff * stepScale;
    }
    return target;
}

static s16 PhotoMode_LERPCeilS(s16 target, s16 cur, f32 stepScale, s16 minDiff) {
    s16 diff = (s16)(target - cur);
    if (ABS(diff) >= minDiff) {
        return (s16)(cur + (s16)(diff * stepScale));
    }
    return target;
}

// ============================================================================
// Photo Mode
// ============================================================================

static Vec3f sPhotoEye = { 0.0f, 0.0f, 0.0f };
static bool  sPhotoInitialized = false;

void Camera_PhotoMode_Init(Camera* camera) {
    sPhotoEye = camera->eye;
    camera->animState = 0;
    sPhotoInitialized = true;
}

s32 Camera_PhotoMode(Camera* camera) {
    camera->fov = 10.0f;   // <-- DEBUG: se o FOV ficar fechado, esta função está rodando

    if (!sPhotoInitialized) {
        Camera_PhotoMode_Init(camera);
    }

    Vec3f* at = &camera->at;
    Vec3f* eye = &camera->eye;
    Vec3f* eyeNext = &camera->eyeNext;
    VecSph eyeAdjustment;

    if (camera->animState == 0) {
        sPhotoEye = *eye;
        camera->animState = 1;
    }

    // Substitui o antigo D_8015BD7C (global do decomp que não existe mais).
    Input* input = &gPlayState->state.input[0];

    // ========== LOOK (right stick + mouse) ==========
    f32 lookX = -input->cur.right_stick_x * 10.0f;
    f32 lookY =  input->cur.right_stick_y * 10.0f;
    Mouse_HandleThirdPerson(&lookX, &lookY);

    f32 sensitivity = CVarGetFloat(CVAR_PHOTOMODE("Sensitivity"), 1.0f);
    camera->play->camX += lookX * sensitivity;
    camera->play->camY += lookY * sensitivity;

    // ========== MOVE (left stick) ==========
    f32 moveX = input->cur.stick_x / 60.0f;
    f32 moveY = input->cur.stick_y / 60.0f;
    f32 speed = CVarGetFloat(CVAR_PHOTOMODE("MoveSpeed"), 5.0f);

    VecSph moveDir;
    moveDir.r = speed * moveY;
    moveDir.yaw = (s16)camera->play->camX;              // <-- cast explícito
    moveDir.pitch = 0;
    PhotoMode_Vec3fVecSphGeoAdd(&sPhotoEye, &sPhotoEye, &moveDir);

    moveDir.r = speed * moveX;
    moveDir.yaw = (s16)(camera->play->camX + 0x4000);   // <-- cast explícito
    moveDir.pitch = 0;
    PhotoMode_Vec3fVecSphGeoAdd(&sPhotoEye, &sPhotoEye, &moveDir);

    // Vertical (C-Up / C-Down)
    if (CHECK_BTN_ANY(input->cur.button, BTN_CUP)) {
        sPhotoEye.y += speed;
    }
    if (CHECK_BTN_ANY(input->cur.button, BTN_CDOWN)) {
        sPhotoEye.y -= speed;
    }

    *eyeNext = *eye = sPhotoEye;

    // ========== OLHAR (at) ==========
    eyeAdjustment.r = 50.0f;
    eyeAdjustment.yaw   = (s16)camera->play->camX;      // <-- cast explícito
    eyeAdjustment.pitch = (s16)camera->play->camY;      // <-- cast explícito
    PhotoMode_Vec3fVecSphGeoAdd(at, eye, &eyeAdjustment);

    // ========== FOV & ROLL ==========
    camera->fov  = PhotoMode_LERPCeilF(CVarGetFloat(CVAR_PHOTOMODE("Fov"), 65.0f),
                                       camera->fov, 0.3f, 0.1f);
    camera->roll = PhotoMode_LERPCeilS(0, camera->roll, 0.5f, 0xA);

    // ========== HIDE HUD ==========
    // `sCameraInterfaceFlags` é `static` dentro de z_camera.c e não pode ser
    // setado daqui. Para esconder o HUD de verdade, use gSaveContext.hudVisibility.
    // if (CVarGetInteger(CVAR_PHOTOMODE("HideHud"), 1)) {
    //     gSaveContext.hudVisibility = 0xF000;
    // }

    return 1;
}

// ============================================================================
// Registro das CVars
// ============================================================================

static void RegisterPhotoModeCVars() {
    CVarRegisterInteger(CVAR_ENHANCEMENT("PhotoMode.Enabled"), 0);
    CVarRegisterInteger(CVAR_ENHANCEMENT("PhotoMode.HideHud"), 1);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.Sensitivity"), 1.0f);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.MoveSpeed"), 5.0f);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.Fov"), 65.0f);
}

static RegisterShipInitFunc initFunc(RegisterPhotoModeCVars, { "" });
