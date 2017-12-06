/** Copyright (c) 2014-present, Facebook, Inc. */

#include "YGNodeList.h"

extern RYGMalloc gRYGMalloc;
extern RYGRealloc gRYGRealloc;
extern RYGFree gRYGFree;

struct RYGNodeList {
  uint32_t capacity;
  uint32_t count;
  RYGNodeRef *items;
};

RYGNodeListRef RYGNodeListNew(const uint32_t initialCapacity) {
  const RYGNodeListRef list = gRYGMalloc(sizeof(struct RYGNodeList));
  RYG_ASSERT(list != NULL, "Could not allocate memory for list");

  list->capacity = initialCapacity;
  list->count = 0;
  list->items = gRYGMalloc(sizeof(RYGNodeRef) * list->capacity);
  RYG_ASSERT(list->items != NULL, "Could not allocate memory for items");

  return list;
}

void RYGNodeListFree(const RYGNodeListRef list) {
  if (list) {
    gRYGFree(list->items);
    gRYGFree(list);
  }
}

uint32_t RYGNodeListCount(const RYGNodeListRef list) {
  if (list) {
    return list->count;
  }
  return 0;
}

void RYGNodeListAdd(RYGNodeListRef *listp, const RYGNodeRef node) {
  if (!*listp) {
    *listp = RYGNodeListNew(4);
  }
  RYGNodeListInsert(listp, node, (*listp)->count);
}

void RYGNodeListInsert(RYGNodeListRef *listp, const RYGNodeRef node, const uint32_t index) {
  if (!*listp) {
    *listp = RYGNodeListNew(4);
  }
  RYGNodeListRef list = *listp;

  if (list->count == list->capacity) {
    list->capacity *= 2;
    list->items = gRYGRealloc(list->items, sizeof(RYGNodeRef) * list->capacity);
    RYG_ASSERT(list->items != NULL, "Could not extend allocation for items");
  }

  for (uint32_t i = list->count; i > index; i--) {
    list->items[i] = list->items[i - 1];
  }

  list->count++;
  list->items[index] = node;
}

RYGNodeRef RYGNodeListRemove(const RYGNodeListRef list, const uint32_t index) {
  const RYGNodeRef removed = list->items[index];
  list->items[index] = NULL;

  for (uint32_t i = index; i < list->count - 1; i++) {
    list->items[i] = list->items[i + 1];
    list->items[i + 1] = NULL;
  }

  list->count--;
  return removed;
}

RYGNodeRef RYGNodeListDelete(const RYGNodeListRef list, const RYGNodeRef node) {
  for (uint32_t i = 0; i < list->count; i++) {
    if (list->items[i] == node) {
      return RYGNodeListRemove(list, i);
    }
  }

  return NULL;
}

RYGNodeRef RYGNodeListGet(const RYGNodeListRef list, const uint32_t index) {
  if (RYGNodeListCount(list) > 0) {
    return list->items[index];
  }

  return NULL;
}
