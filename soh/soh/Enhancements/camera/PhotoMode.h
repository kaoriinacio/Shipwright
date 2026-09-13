#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Chamado quando o Photo Mode é ativado pela primeira vez
void Camera_PhotoMode_Init(Camera* camera);

// Função principal do modo câmera
s32 Camera_PhotoMode(Camera* camera);

#ifdef __cplusplus
}
#endif