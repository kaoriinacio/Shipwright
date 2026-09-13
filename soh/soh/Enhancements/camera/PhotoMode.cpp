#include "PhotoMode.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/controls/Mouse.h"
#include "soh/ShipInit.hpp"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "global.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define CVAR_PHOTOMODE(name) CVAR_ENHANCEMENT("PhotoMode." name)

static Vec3f sPhotoEye = { 0.0f, 0.0f, 0.0f };
static bool  sPhotoInitialized = false;

void Camera_PhotoMode_Init(Camera* camera) {
    sPhotoEye = camera->eye;
    camera->animState = 0;
    sPhotoInitialized = true;
}

s32 Camera_PhotoMode(Camera* camera) {
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

    // ========== LOOK (right stick + mouse) ==========
    f32 lookX = -D_8015BD7C->state.input[0].cur.right_stick_x * 10.0f;
    f32 lookY =  D_8015BD7C->state.input[0].cur.right_stick_y * 10.0f;
    Mouse_HandleThirdPerson(&lookX, &lookY);

    f32 sensitivity = CVarGetFloat(CVAR_PHOTOMODE("Sensitivity"), 1.0f);
    camera->play->camX += lookX * sensitivity;
    camera->play->camY += lookY * sensitivity;

    // ========== MOVE (left stick) ==========
    f32 moveX = D_8015BD7C->state.input[0].cur.stick_x / 60.0f;
    f32 moveY = D_8015BD7C->state.input[0].cur.stick_y / 60.0f;
    f32 speed = CVarGetFloat(CVAR_PHOTOMODE("MoveSpeed"), 5.0f);

    VecSph moveDir;
    moveDir.r = speed * moveY;
    moveDir.yaw = camera->play->camX;
    moveDir.pitch = 0;
    Camera_Vec3fVecSphGeoAdd(&sPhotoEye, &sPhotoEye, &moveDir);

    moveDir.r = speed * moveX;
    moveDir.yaw = camera->play->camX + 0x4000;
    moveDir.pitch = 0;
    Camera_Vec3fVecSphGeoAdd(&sPhotoEye, &sPhotoEye, &moveDir);

    // Vertical (C-Up / C-Down)
    if (CHECK_BTN_ANY(D_8015BD7C->state.input[0].cur.button, BTN_CUP)) {
        sPhotoEye.y += speed;
    }
    if (CHECK_BTN_ANY(D_8015BD7C->state.input[0].cur.button, BTN_CDOWN)) {
        sPhotoEye.y -= speed;
    }

    *eyeNext = *eye = sPhotoEye;

    // ========== OLHAR ==========
    eyeAdjustment.r = 50.0f;
    eyeAdjustment.yaw = camera->play->camX;
    eyeAdjustment.pitch = camera->play->camY;
    Camera_Vec3fVecSphGeoAdd(at, eye, &eyeAdjustment);

    // ========== FOV & ROLL ==========
    camera->fov = Camera_LERPCeilF(CVarGetFloat(CVAR_PHOTOMODE("Fov"), 65.0f), camera->fov, 0.3f, 0.1f);
    camera->roll = Camera_LERPCeilS(0, camera->roll, 0.5f, 0xA);

    // ========== HIDE HUD ==========
    if (CVarGetInteger(CVAR_PHOTOMODE("HideHud"), 1)) {
        sCameraInterfaceFlags = 0xF000;
    }

    return 1;
}

// Registra as CVars
static void RegisterPhotoModeCVars() {
    CVarRegisterInteger(CVAR_ENHANCEMENT("PhotoMode.Enabled"), 0);
    CVarRegisterInteger(CVAR_ENHANCEMENT("PhotoMode.HideHud"), 1);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.Sensitivity"), 1.0f);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.MoveSpeed"), 5.0f);
    CVarRegisterFloat(CVAR_ENHANCEMENT("PhotoMode.Fov"), 65.0f);
}

static RegisterShipInitFunc initFunc(RegisterPhotoModeCVars, { "" });