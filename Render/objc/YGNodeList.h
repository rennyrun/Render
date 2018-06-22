/** Copyright (c) 2014-present, Facebook, Inc. */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "YGMacros.h"
#include "Yoga.h"

RYG_EXTERN_C_BEGIN

typedef struct RYGNodeList *RYGNodeListRef;

RYGNodeListRef RYGNodeListNew(const uint32_t initialCapacity);
void RYGNodeListFree(const RYGNodeListRef list);
uint32_t RYGNodeListCount(const RYGNodeListRef list);
void RYGNodeListAdd(RYGNodeListRef *listp, const RYGNodeRef node);
void RYGNodeListInsert(RYGNodeListRef *listp, const RYGNodeRef node, const uint32_t index);
RYGNodeRef RYGNodeListRemove(const RYGNodeListRef list, const uint32_t index);
RYGNodeRef RYGNodeListDelete(const RYGNodeListRef list, const RYGNodeRef node);
RYGNodeRef RYGNodeListGet(const RYGNodeListRef list, const uint32_t index);

RYG_EXTERN_C_END
