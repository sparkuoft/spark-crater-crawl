#ifndef PILLAR_FSM_H
#define PILLAR_FSM_H

#include <stdint.h>
#include "shared_state.h"

void pillarFsmInit(uint32_t now_ms);

bool pillarStartWarning(uint8_t id, WarningMode mode, bool isFake, uint32_t now_ms);

void pillarFsmUpdate(uint32_t now_ms);

#endif