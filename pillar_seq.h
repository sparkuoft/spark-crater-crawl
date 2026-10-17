#ifndef PILLAR_SEQ_H
#define PILLAR_SEQ_H

#include <stdint.h>
#include <stdlib.h>
#include "shared_state.h"
#include "pillar_fsm.h"

void pillarSeqInit(uint32_t now_ms);

void pillarSeqUpdate(uint32_t now_ms);

#endif