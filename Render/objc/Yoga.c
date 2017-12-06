/** Copyright (c) 2014-present, Facebook, Inc. */

#include <string.h>

#include "YGNodeList.h"
#include "Yoga.h"

#ifdef _MSC_VER
#include <float.h>
#ifndef isnan
#define isnan _isnan
#endif

#ifndef __cplusplus
#define inline __inline
#endif

/* define fmaxf if < VC12 */
#if _MSC_VER < 1800
__forceinline const float fmaxf(const float a, const float b) {
  return (a > b) ? a : b;
}
#endif
#endif

typedef struct RYGCachedMeasurement {
  float availableWidth;
  float availableHeight;
  RYGMeasureMode widthMeasureMode;
  RYGMeasureMode heightMeasureMode;

  float computedWidth;
  float computedHeight;
} RYGCachedMeasurement;

// This value was chosen based on empiracle data. Even the most complicated
// layouts should not require more than 16 entries to fit within the cache.
#define RYG_MAX_CACHED_RESULT_COUNT 16

typedef struct RYGLayout {
  float position[4];
  float dimensions[2];
  float margin[6];
  float border[6];
  float padding[6];
  RYGDirection direction;

  uint32_t computedFlexBasisGeneration;
  float computedFlexBasis;

  // Instead of recomputing the entire layout every single time, we
  // cache some information to break early when nothing changed
  uint32_t generationCount;
  RYGDirection lastParentDirection;

  uint32_t nextCachedMeasurementsIndex;
  RYGCachedMeasurement cachedMeasurements[RYG_MAX_CACHED_RESULT_COUNT];
  float measuredDimensions[2];

  RYGCachedMeasurement cachedLayout;
} RYGLayout;

typedef struct RYGStyle {
  RYGDirection direction;
  RYGFlexDirection flexDirection;
  RYGJustify justifyContent;
  RYGAlign alignContent;
  RYGAlign alignItems;
  RYGAlign alignSelf;
  RYGPositionType positionType;
  RYGWrap flexWrap;
  RYGOverflow overflow;
  RYGDisplay display;
  float flex;
  float flexGrow;
  float flexShrink;
  RYGValue flexBasis;
  RYGValue margin[RYGEdgeCount];
  RYGValue position[RYGEdgeCount];
  RYGValue padding[RYGEdgeCount];
  RYGValue border[RYGEdgeCount];
  RYGValue dimensions[2];
  RYGValue minDimensions[2];
  RYGValue maxDimensions[2];

  // Yoga specific properties, not compatible with flexbox specification
  float aspectRatio;
} RYGStyle;

typedef struct RYGNode {
  RYGStyle style;
  RYGLayout layout;
  uint32_t lineIndex;

  RYGNodeRef parent;
  RYGNodeListRef children;

  struct RYGNode *nextChild;

  RYGMeasureFunc measure;
  RYGBaselineFunc baseline;
  RYGPrintFunc print;
  void *context;

  bool isDirty;
  bool hasNewLayout;

  RYGValue const *resolvedDimensions[2];
} RYGNode;

#define RYG_UNDEFINED_VALUES \
{ .value = RYGUndefined, .unit = RYGUnitUndefined }

#define RYG_AUTO_VALUES \
{ .value = RYGUndefined, .unit = RYGUnitAuto }

#define RYG_DEFAULT_EDGE_VALUES_UNIT                                                   \
{                                                                                   \
[RYGEdgeLeft] = RYG_UNDEFINED_VALUES, [RYGEdgeTop] = RYG_UNDEFINED_VALUES,            \
[RYGEdgeRight] = RYG_UNDEFINED_VALUES, [RYGEdgeBottom] = RYG_UNDEFINED_VALUES,        \
[RYGEdgeStart] = RYG_UNDEFINED_VALUES, [RYGEdgeEnd] = RYG_UNDEFINED_VALUES,           \
[RYGEdgeHorizontal] = RYG_UNDEFINED_VALUES, [RYGEdgeVertical] = RYG_UNDEFINED_VALUES, \
[RYGEdgeAll] = RYG_UNDEFINED_VALUES,                                                \
}

#define RYG_DEFAULT_DIMENSION_VALUES \
{ [RYGDimensionWidth] = RYGUndefined, [RYGDimensionHeight] = RYGUndefined, }

#define RYG_DEFAULT_DIMENSION_VALUES_UNIT \
{ [RYGDimensionWidth] = RYG_UNDEFINED_VALUES, [RYGDimensionHeight] = RYG_UNDEFINED_VALUES, }

#define RYG_DEFAULT_DIMENSION_VALUES_AUTO_UNIT \
{ [RYGDimensionWidth] = RYG_AUTO_VALUES, [RYGDimensionHeight] = RYG_AUTO_VALUES, }

static RYGNode gRYGNodeDefaults = {
  .parent = NULL,
  .children = NULL,
  .hasNewLayout = true,
  .isDirty = false,
  .resolvedDimensions = {[RYGDimensionWidth] = &RYGValueUndefined,
    [RYGDimensionHeight] = &RYGValueUndefined},

  .style =
  {
    .flex = RYGUndefined,
    .flexGrow = RYGUndefined,
    .flexShrink = RYGUndefined,
    .flexBasis = RYG_AUTO_VALUES,
    .justifyContent = RYGJustifyFlexStart,
    .alignItems = RYGAlignStretch,
    .alignContent = RYGAlignFlexStart,
    .direction = RYGDirectionInherit,
    .flexDirection = RYGFlexDirectionColumn,
    .overflow = RYGOverflowVisible,
    .display = RYGDisplayFlex,
    .dimensions = RYG_DEFAULT_DIMENSION_VALUES_AUTO_UNIT,
    .minDimensions = RYG_DEFAULT_DIMENSION_VALUES_UNIT,
    .maxDimensions = RYG_DEFAULT_DIMENSION_VALUES_UNIT,
    .position = RYG_DEFAULT_EDGE_VALUES_UNIT,
    .margin = RYG_DEFAULT_EDGE_VALUES_UNIT,
    .padding = RYG_DEFAULT_EDGE_VALUES_UNIT,
    .border = RYG_DEFAULT_EDGE_VALUES_UNIT,
    .aspectRatio = RYGUndefined,
  },

  .layout =
  {
    .dimensions = RYG_DEFAULT_DIMENSION_VALUES,
    .lastParentDirection = (RYGDirection) -1,
    .nextCachedMeasurementsIndex = 0,
    .computedFlexBasis = RYGUndefined,
    .measuredDimensions = RYG_DEFAULT_DIMENSION_VALUES,

    .cachedLayout =
    {
      .widthMeasureMode = (RYGMeasureMode) -1,
      .heightMeasureMode = (RYGMeasureMode) -1,
      .computedWidth = -1,
      .computedHeight = -1,
    },
  },
};

static void RYGNodeMarkDirtyInternal(const RYGNodeRef node);

RYGMalloc gRYGMalloc = &malloc;
RYGCalloc gRYGCalloc = &calloc;
RYGRealloc gRYGRealloc = &realloc;
RYGFree gRYGFree = &free;

static RYGValue RYGValueZero = {.value = 0, .unit = RYGUnitPoint};

#ifdef ANDROID
#include <android/log.h>
static int RYGAndroidLog(RYGLogLevel level, const char *format, va_list args) {
  int androidLevel = RYGLogLevelDebug;
  switch (level) {
    case RYGLogLevelError:
      androidLevel = ANDROID_LOG_ERROR;
      break;
    case RYGLogLevelWarn:
      androidLevel = ANDROID_LOG_WARN;
      break;
    case RYGLogLevelInfo:
      androidLevel = ANDROID_LOG_INFO;
      break;
    case RYGLogLevelDebug:
      androidLevel = ANDROID_LOG_DEBUG;
      break;
    case RYGLogLevelVerbose:
      androidLevel = ANDROID_LOG_VERBOSE;
      break;
  }
  const int result = __android_log_vprint(androidLevel, "RYG-layout", format, args);
  return result;
}
static RYGLogger gLogger = &RYGAndroidLog;
#else
static int RYGDefaultLog(RYGLogLevel level, const char *format, va_list args) {
  switch (level) {
    case RYGLogLevelError:
      return vfprintf(stderr, format, args);
    case RYGLogLevelWarn:
    case RYGLogLevelInfo:
    case RYGLogLevelDebug:
    case RYGLogLevelVerbose:
    default:
      return vprintf(format, args);
  }
}
static RYGLogger gLogger = &RYGDefaultLog;
#endif

static inline const RYGValue *RYGComputedEdgeValue(const RYGValue edges[RYGEdgeCount],
                                                 const RYGEdge edge,
                                                 const RYGValue *const defaultValue) {
  RYG_ASSERT(edge <= RYGEdgeEnd, "Cannot get computed value of multi-edge shorthands");

  if (edges[edge].unit != RYGUnitUndefined) {
    return &edges[edge];
  }

  if ((edge == RYGEdgeTop || edge == RYGEdgeBottom) &&
      edges[RYGEdgeVertical].unit != RYGUnitUndefined) {
    return &edges[RYGEdgeVertical];
  }

  if ((edge == RYGEdgeLeft || edge == RYGEdgeRight || edge == RYGEdgeStart || edge == RYGEdgeEnd) &&
      edges[RYGEdgeHorizontal].unit != RYGUnitUndefined) {
    return &edges[RYGEdgeHorizontal];
  }

  if (edges[RYGEdgeAll].unit != RYGUnitUndefined) {
    return &edges[RYGEdgeAll];
  }

  if (edge == RYGEdgeStart || edge == RYGEdgeEnd) {
    return &RYGValueUndefined;
  }

  return defaultValue;
}

static inline float RYGValueResolve(const RYGValue *const value, const float parentSize) {
  switch (value->unit) {
    case RYGUnitUndefined:
    case RYGUnitAuto:
      return RYGUndefined;
    case RYGUnitPoint:
      return value->value;
    case RYGUnitPercent:
      return value->value * parentSize / 100.0f;
  }
  return RYGUndefined;
}

static inline float RYGValueResolveMargin(const RYGValue *const value, const float parentSize) {
  return value->unit == RYGUnitAuto ? 0 : RYGValueResolve(value, parentSize);
}

int32_t gNodeInstanceCount = 0;

RYGNodeRef RYGNodeNew(void) {
  const RYGNodeRef node = gRYGMalloc(sizeof(RYGNode));
  RYG_ASSERT(node, "Could not allocate memory for node");
  gNodeInstanceCount++;

  memcpy(node, &gRYGNodeDefaults, sizeof(RYGNode));
  return node;
}

void RYGNodeFree(const RYGNodeRef node) {
  if (node->parent) {
    RYGNodeListDelete(node->parent->children, node);
    node->parent = NULL;
  }

  const uint32_t childCount = RYGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; i++) {
    const RYGNodeRef child = RYGNodeGetChild(node, i);
    child->parent = NULL;
  }

  RYGNodeListFree(node->children);
  gRYGFree(node);
  gNodeInstanceCount--;
}

void RYGNodeFreeRecursive(const RYGNodeRef root) {
  while (RYGNodeGetChildCount(root) > 0) {
    const RYGNodeRef child = RYGNodeGetChild(root, 0);
    RYGNodeRemoveChild(root, child);
    RYGNodeFreeRecursive(child);
  }
  RYGNodeFree(root);
}

void RYGNodeReset(const RYGNodeRef node) {
  RYG_ASSERT(RYGNodeGetChildCount(node) == 0,
            "Cannot reset a node which still has children attached");
  RYG_ASSERT(node->parent == NULL, "Cannot reset a node still attached to a parent");

  RYGNodeListFree(node->children);
  memcpy(node, &gRYGNodeDefaults, sizeof(RYGNode));
}

int32_t RYGNodeGetInstanceCount(void) {
  return gNodeInstanceCount;
}

static void RYGNodeMarkDirtyInternal(const RYGNodeRef node) {
  if (!node->isDirty) {
    node->isDirty = true;
    node->layout.computedFlexBasis = RYGUndefined;
    if (node->parent) {
      RYGNodeMarkDirtyInternal(node->parent);
    }
  }
}

void RYGNodeSetMeasureFunc(const RYGNodeRef node, RYGMeasureFunc measureFunc) {
  if (measureFunc == NULL) {
    node->measure = NULL;
  } else {
    node->measure = measureFunc;
  }
}

RYGMeasureFunc RYGNodeGetMeasureFunc(const RYGNodeRef node) {
  return node->measure;
}

void RYGNodeSetBaselineFunc(const RYGNodeRef node, RYGBaselineFunc baselineFunc) {
  node->baseline = baselineFunc;
}

RYGBaselineFunc RYGNodeGetBaselineFunc(const RYGNodeRef node) {
  return node->baseline;
}

void RYGNodeInsertChild(const RYGNodeRef node, const RYGNodeRef child, const uint32_t index) {
  RYG_ASSERT(child->parent == NULL, "Child already has a parent, it must be removed first.");
  RYG_ASSERT(node->measure == NULL,
            "Cannot add child: Nodes with measure functions cannot have children.");
  RYGNodeListInsert(&node->children, child, index);
  child->parent = node;
  RYGNodeMarkDirtyInternal(node);
}

void RYGNodeRemoveChild(const RYGNodeRef node, const RYGNodeRef child) {
  if (RYGNodeListDelete(node->children, child) != NULL) {
    child->parent = NULL;
    RYGNodeMarkDirtyInternal(node);
  }
}

RYGNodeRef RYGNodeGetChild(const RYGNodeRef node, const uint32_t index) {
  return RYGNodeListGet(node->children, index);
}

RYGNodeRef RYGNodeGetParent(const RYGNodeRef node) {
  return node->parent;
}

inline uint32_t RYGNodeGetChildCount(const RYGNodeRef node) {
  return RYGNodeListCount(node->children);
}

void RYGNodeMarkDirty(const RYGNodeRef node) {
  RYG_ASSERT(node->measure != NULL,
            "Only leaf nodes with custom measure functions"
            "should manually mark themselves as dirty");
  RYGNodeMarkDirtyInternal(node);
}

bool RYGNodeIsDirty(const RYGNodeRef node) {
  return node->isDirty;
}

void RYGNodeCopyStyle(const RYGNodeRef dstNode, const RYGNodeRef srcNode) {
  if (memcmp(&dstNode->style, &srcNode->style, sizeof(RYGStyle)) != 0) {
    memcpy(&dstNode->style, &srcNode->style, sizeof(RYGStyle));
    RYGNodeMarkDirtyInternal(dstNode);
  }
}

inline float RYGNodeStyleGetFlexGrow(const RYGNodeRef node) {
  if (!RYGFloatIsUndefined(node->style.flexGrow)) {
    return node->style.flexGrow;
  }
  if (!RYGFloatIsUndefined(node->style.flex) && node->style.flex > 0.0f) {
    return node->style.flex;
  }
  return 0.0f;
}

inline float RYGNodeStyleGetFlexShrink(const RYGNodeRef node) {
  if (!RYGFloatIsUndefined(node->style.flexShrink)) {
    return node->style.flexShrink;
  }
  if (!RYGFloatIsUndefined(node->style.flex) && node->style.flex < 0.0f) {
    return -node->style.flex;
  }
  return 0.0f;
}

static inline const RYGValue *RYGNodeStyleGetFlexBasisPtr(const RYGNodeRef node) {
  if (node->style.flexBasis.unit != RYGUnitAuto && node->style.flexBasis.unit != RYGUnitUndefined) {
    return &node->style.flexBasis;
  }
  if (!RYGFloatIsUndefined(node->style.flex) && node->style.flex > 0.0f) {
    return &RYGValueZero;
  }
  return &RYGValueAuto;
}

inline RYGValue RYGNodeStyleGetFlexBasis(const RYGNodeRef node) {
  return *RYGNodeStyleGetFlexBasisPtr(node);
}

void RYGNodeStyleSetFlex(const RYGNodeRef node, const float flex) {
  if (node->style.flex != flex) {
    node->style.flex = flex;
    RYGNodeMarkDirtyInternal(node);
  }
}

#define RYG_NODE_PROPERTY_IMPL(type, name, paramName, instanceName) \
void RYGNodeSet##name(const RYGNodeRef node, type paramName) {     \
node->instanceName = paramName;                                \
}                                                                \
\
type RYGNodeGet##name(const RYGNodeRef node) {                     \
return node->instanceName;                                     \
}

#define RYG_NODE_STYLE_PROPERTY_SETTER_IMPL(type, name, paramName, instanceName) \
void RYGNodeStyleSet##name(const RYGNodeRef node, const type paramName) {       \
if (node->style.instanceName != paramName) {                                \
node->style.instanceName = paramName;                                     \
RYGNodeMarkDirtyInternal(node);                                            \
}                                                                           \
}

#define RYG_NODE_STYLE_PROPERTY_SETTER_UNIT_IMPL(type, name, paramName, instanceName) \
void RYGNodeStyleSet##name(const RYGNodeRef node, const type paramName) {            \
if (node->style.instanceName.value != paramName ||                               \
node->style.instanceName.unit != RYGUnitPoint) {                              \
node->style.instanceName.value = paramName;                                    \
node->style.instanceName.unit =                                                \
RYGFloatIsUndefined(paramName) ? RYGUnitAuto : RYGUnitPoint;                  \
RYGNodeMarkDirtyInternal(node);                                                 \
}                                                                                \
}                                                                                  \
\
void RYGNodeStyleSet##name##Percent(const RYGNodeRef node, const type paramName) {   \
if (node->style.instanceName.value != paramName ||                               \
node->style.instanceName.unit != RYGUnitPercent) {                            \
node->style.instanceName.value = paramName;                                    \
node->style.instanceName.unit =                                                \
RYGFloatIsUndefined(paramName) ? RYGUnitAuto : RYGUnitPercent;                \
RYGNodeMarkDirtyInternal(node);                                                 \
}                                                                                \
}

#define RYG_NODE_STYLE_PROPERTY_SETTER_UNIT_AUTO_IMPL(type, name, paramName, instanceName)         \
void RYGNodeStyleSet##name(const RYGNodeRef node, const type paramName) {                         \
if (node->style.instanceName.value != paramName ||                                            \
node->style.instanceName.unit != RYGUnitPoint) {                                           \
node->style.instanceName.value = paramName;                                                 \
node->style.instanceName.unit = RYGFloatIsUndefined(paramName) ? RYGUnitAuto : RYGUnitPoint;   \
RYGNodeMarkDirtyInternal(node);                                                              \
}                                                                                             \
}                                                                                               \
\
void RYGNodeStyleSet##name##Percent(const RYGNodeRef node, const type paramName) {                \
if (node->style.instanceName.value != paramName ||                                            \
node->style.instanceName.unit != RYGUnitPercent) {                                         \
node->style.instanceName.value = paramName;                                                 \
node->style.instanceName.unit = RYGFloatIsUndefined(paramName) ? RYGUnitAuto : RYGUnitPercent; \
RYGNodeMarkDirtyInternal(node);                                                              \
}                                                                                             \
}                                                                                               \
\
void RYGNodeStyleSet##name##Auto(const RYGNodeRef node) {                                         \
if (node->style.instanceName.unit != RYGUnitAuto) {                                            \
node->style.instanceName.value = RYGUndefined;                                               \
node->style.instanceName.unit = RYGUnitAuto;                                                 \
RYGNodeMarkDirtyInternal(node);                                                              \
}                                                                                             \
}

#define RYG_NODE_STYLE_PROPERTY_IMPL(type, name, paramName, instanceName)  \
RYG_NODE_STYLE_PROPERTY_SETTER_IMPL(type, name, paramName, instanceName) \
\
type RYGNodeStyleGet##name(const RYGNodeRef node) {                       \
return node->style.instanceName;                                      \
}

#define RYG_NODE_STYLE_PROPERTY_UNIT_IMPL(type, name, paramName, instanceName)   \
RYG_NODE_STYLE_PROPERTY_SETTER_UNIT_IMPL(float, name, paramName, instanceName) \
\
type RYGNodeStyleGet##name(const RYGNodeRef node) {                             \
return node->style.instanceName;                                            \
}

#define RYG_NODE_STYLE_PROPERTY_UNIT_AUTO_IMPL(type, name, paramName, instanceName)   \
RYG_NODE_STYLE_PROPERTY_SETTER_UNIT_AUTO_IMPL(float, name, paramName, instanceName) \
\
type RYGNodeStyleGet##name(const RYGNodeRef node) {                                  \
return node->style.instanceName;                                                 \
}

#define RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_AUTO_IMPL(type, name, instanceName) \
void RYGNodeStyleSet##name##Auto(const RYGNodeRef node, const RYGEdge edge) { \
if (node->style.instanceName[edge].unit != RYGUnitAuto) {                 \
node->style.instanceName[edge].value = RYGUndefined;                    \
node->style.instanceName[edge].unit = RYGUnitAuto;                      \
RYGNodeMarkDirtyInternal(node);                                         \
}                                                                        \
}

#define RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_IMPL(type, name, paramName, instanceName)            \
void RYGNodeStyleSet##name(const RYGNodeRef node, const RYGEdge edge, const float paramName) { \
if (node->style.instanceName[edge].value != paramName ||                                  \
node->style.instanceName[edge].unit != RYGUnitPoint) {                                 \
node->style.instanceName[edge].value = paramName;                                       \
node->style.instanceName[edge].unit =                                                   \
RYGFloatIsUndefined(paramName) ? RYGUnitUndefined : RYGUnitPoint;                      \
RYGNodeMarkDirtyInternal(node);                                                          \
}                                                                                         \
}                                                                                           \
\
void RYGNodeStyleSet##name##Percent(const RYGNodeRef node,                                    \
const RYGEdge edge,                                       \
const float paramName) {                                 \
if (node->style.instanceName[edge].value != paramName ||                                  \
node->style.instanceName[edge].unit != RYGUnitPercent) {                               \
node->style.instanceName[edge].value = paramName;                                       \
node->style.instanceName[edge].unit =                                                   \
RYGFloatIsUndefined(paramName) ? RYGUnitUndefined : RYGUnitPercent;                    \
RYGNodeMarkDirtyInternal(node);                                                          \
}                                                                                         \
}                                                                                           \
\
type RYGNodeStyleGet##name(const RYGNodeRef node, const RYGEdge edge) {                        \
return node->style.instanceName[edge];                                                    \
}

#define RYG_NODE_STYLE_EDGE_PROPERTY_IMPL(type, name, paramName, instanceName)                 \
void RYGNodeStyleSet##name(const RYGNodeRef node, const RYGEdge edge, const float paramName) { \
if (node->style.instanceName[edge].value != paramName ||                                  \
node->style.instanceName[edge].unit != RYGUnitPoint) {                                 \
node->style.instanceName[edge].value = paramName;                                       \
node->style.instanceName[edge].unit =                                                   \
RYGFloatIsUndefined(paramName) ? RYGUnitUndefined : RYGUnitPoint;                      \
RYGNodeMarkDirtyInternal(node);                                                          \
}                                                                                         \
}                                                                                           \
\
float RYGNodeStyleGet##name(const RYGNodeRef node, const RYGEdge edge) {                       \
return node->style.instanceName[edge].value;                                              \
}

#define RYG_NODE_LAYOUT_PROPERTY_IMPL(type, name, instanceName) \
type RYGNodeLayoutGet##name(const RYGNodeRef node) {           \
return node->layout.instanceName;                          \
}

#define RYG_NODE_LAYOUT_RESOLVED_PROPERTY_IMPL(type, name, instanceName)                    \
type RYGNodeLayoutGet##name(const RYGNodeRef node, const RYGEdge edge) {                    \
RYG_ASSERT(edge <= RYGEdgeEnd, "Cannot get layout properties of multi-edge shorthands"); \
\
if (edge == RYGEdgeLeft) {                                                              \
if (node->layout.direction == RYGDirectionRTL) {                                      \
return node->layout.instanceName[RYGEdgeEnd];                                       \
} else {                                                                             \
return node->layout.instanceName[RYGEdgeStart];                                     \
}                                                                                    \
}                                                                                      \
\
if (edge == RYGEdgeRight) {                                                             \
if (node->layout.direction == RYGDirectionRTL) {                                      \
return node->layout.instanceName[RYGEdgeStart];                                     \
} else {                                                                             \
return node->layout.instanceName[RYGEdgeEnd];                                       \
}                                                                                    \
}                                                                                      \
\
return node->layout.instanceName[edge];                                                \
}

RYG_NODE_PROPERTY_IMPL(void *, Context, context, context);
RYG_NODE_PROPERTY_IMPL(RYGPrintFunc, PrintFunc, printFunc, print);
RYG_NODE_PROPERTY_IMPL(bool, HasNewLayout, hasNewLayout, hasNewLayout);

RYG_NODE_STYLE_PROPERTY_IMPL(RYGDirection, Direction, direction, direction);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGFlexDirection, FlexDirection, flexDirection, flexDirection);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGJustify, JustifyContent, justifyContent, justifyContent);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGAlign, AlignContent, alignContent, alignContent);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGAlign, AlignItems, alignItems, alignItems);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGAlign, AlignSelf, alignSelf, alignSelf);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGPositionType, PositionType, positionType, positionType);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGWrap, FlexWrap, flexWrap, flexWrap);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGOverflow, Overflow, overflow, overflow);
RYG_NODE_STYLE_PROPERTY_IMPL(RYGDisplay, Display, display, display);

RYG_NODE_STYLE_PROPERTY_SETTER_IMPL(float, FlexGrow, flexGrow, flexGrow);
RYG_NODE_STYLE_PROPERTY_SETTER_IMPL(float, FlexShrink, flexShrink, flexShrink);
RYG_NODE_STYLE_PROPERTY_SETTER_UNIT_AUTO_IMPL(float, FlexBasis, flexBasis, flexBasis);

RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_IMPL(RYGValue, Position, position, position);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_IMPL(RYGValue, Margin, margin, margin);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_AUTO_IMPL(RYGValue, Margin, margin);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_IMPL(RYGValue, Padding, padding, padding);
RYG_NODE_STYLE_EDGE_PROPERTY_IMPL(float, Border, border, border);

RYG_NODE_STYLE_PROPERTY_UNIT_AUTO_IMPL(RYGValue, Width, width, dimensions[RYGDimensionWidth]);
RYG_NODE_STYLE_PROPERTY_UNIT_AUTO_IMPL(RYGValue, Height, height, dimensions[RYGDimensionHeight]);
RYG_NODE_STYLE_PROPERTY_UNIT_IMPL(RYGValue, MinWidth, minWidth, minDimensions[RYGDimensionWidth]);
RYG_NODE_STYLE_PROPERTY_UNIT_IMPL(RYGValue, MinHeight, minHeight, minDimensions[RYGDimensionHeight]);
RYG_NODE_STYLE_PROPERTY_UNIT_IMPL(RYGValue, MaxWidth, maxWidth, maxDimensions[RYGDimensionWidth]);
RYG_NODE_STYLE_PROPERTY_UNIT_IMPL(RYGValue, MaxHeight, maxHeight, maxDimensions[RYGDimensionHeight]);

// Yoga specific properties, not compatible with flexbox specification
RYG_NODE_STYLE_PROPERTY_IMPL(float, AspectRatio, aspectRatio, aspectRatio);

RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Left, position[RYGEdgeLeft]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Top, position[RYGEdgeTop]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Right, position[RYGEdgeRight]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Bottom, position[RYGEdgeBottom]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Width, dimensions[RYGDimensionWidth]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(float, Height, dimensions[RYGDimensionHeight]);
RYG_NODE_LAYOUT_PROPERTY_IMPL(RYGDirection, Direction, direction);

RYG_NODE_LAYOUT_RESOLVED_PROPERTY_IMPL(float, Margin, margin);
RYG_NODE_LAYOUT_RESOLVED_PROPERTY_IMPL(float, Border, border);
RYG_NODE_LAYOUT_RESOLVED_PROPERTY_IMPL(float, Padding, padding);

uint32_t gCurrentGenerationCount = 0;

bool RYGLayoutNodeInternal(const RYGNodeRef node,
                          const float availableWidth,
                          const float availableHeight,
                          const RYGDirection parentDirection,
                          const RYGMeasureMode widthMeasureMode,
                          const RYGMeasureMode heightMeasureMode,
                          const float parentWidth,
                          const float parentHeight,
                          const bool performLayout,
                          const char *reason);

inline bool RYGFloatIsUndefined(const float value) {
  return isnan(value);
}

static inline bool RYGValueEqual(const RYGValue a, const RYGValue b) {
  if (a.unit != b.unit) {
    return false;
  }

  if (a.unit == RYGUnitUndefined) {
    return true;
  }

  return fabs(a.value - b.value) < 0.0001f;
}

static inline void RYGResolveDimensions(RYGNodeRef node) {
  for (RYGDimension dim = RYGDimensionWidth; dim <= RYGDimensionHeight; dim++) {
    if (node->style.maxDimensions[dim].unit != RYGUnitUndefined &&
        RYGValueEqual(node->style.maxDimensions[dim], node->style.minDimensions[dim])) {
      node->resolvedDimensions[dim] = &node->style.maxDimensions[dim];
    } else {
      node->resolvedDimensions[dim] = &node->style.dimensions[dim];
    }
  }
}

static inline bool RYGFloatsEqual(const float a, const float b) {
  if (RYGFloatIsUndefined(a)) {
    return RYGFloatIsUndefined(b);
  }
  return fabs(a - b) < 0.0001f;
}

static void RYGIndent(const uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    RYGLog(RYGLogLevelDebug, "  ");
  }
}

static void RYGPrintNumberIfNotZero(const char *str, const RYGValue *const number) {
  if (!RYGFloatsEqual(number->value, 0)) {
    RYGLog(RYGLogLevelDebug,
          "%s: %g%s, ",
          str,
          number->value,
          number->unit == RYGUnitPoint ? "pt" : "%");
  }
}

static void RYGPrintNumberIfNotUndefinedf(const char *str, const float number) {
  if (!RYGFloatIsUndefined(number)) {
    RYGLog(RYGLogLevelDebug, "%s: %g, ", str, number);
  }
}

static void RYGPrintNumberIfNotUndefined(const char *str, const RYGValue *const number) {
  if (number->unit != RYGUnitUndefined) {
    RYGLog(RYGLogLevelDebug,
          "%s: %g%s, ",
          str,
          number->value,
          number->unit == RYGUnitPoint ? "pt" : "%");
  }
}

static bool RYGFourValuesEqual(const RYGValue four[4]) {
  return RYGValueEqual(four[0], four[1]) && RYGValueEqual(four[0], four[2]) &&
  RYGValueEqual(four[0], four[3]);
}

static void RYGNodePrintInternal(const RYGNodeRef node,
                                const RYGPrintOptions options,
                                const uint32_t level) {
  RYGIndent(level);
  RYGLog(RYGLogLevelDebug, "{");

  if (node->print) {
    node->print(node);
  }

  if (options & RYGPrintOptionsLayout) {
    RYGLog(RYGLogLevelDebug, "layout: {");
    RYGLog(RYGLogLevelDebug, "width: %g, ", node->layout.dimensions[RYGDimensionWidth]);
    RYGLog(RYGLogLevelDebug, "height: %g, ", node->layout.dimensions[RYGDimensionHeight]);
    RYGLog(RYGLogLevelDebug, "top: %g, ", node->layout.position[RYGEdgeTop]);
    RYGLog(RYGLogLevelDebug, "left: %g", node->layout.position[RYGEdgeLeft]);
    RYGLog(RYGLogLevelDebug, "}, ");
  }

  if (options & RYGPrintOptionsStyle) {
    if (node->style.flexDirection == RYGFlexDirectionColumn) {
      RYGLog(RYGLogLevelDebug, "flexDirection: 'column', ");
    } else if (node->style.flexDirection == RYGFlexDirectionColumnReverse) {
      RYGLog(RYGLogLevelDebug, "flexDirection: 'column-reverse', ");
    } else if (node->style.flexDirection == RYGFlexDirectionRow) {
      RYGLog(RYGLogLevelDebug, "flexDirection: 'row', ");
    } else if (node->style.flexDirection == RYGFlexDirectionRowReverse) {
      RYGLog(RYGLogLevelDebug, "flexDirection: 'row-reverse', ");
    }

    if (node->style.justifyContent == RYGJustifyCenter) {
      RYGLog(RYGLogLevelDebug, "justifyContent: 'center', ");
    } else if (node->style.justifyContent == RYGJustifyFlexEnd) {
      RYGLog(RYGLogLevelDebug, "justifyContent: 'flex-end', ");
    } else if (node->style.justifyContent == RYGJustifySpaceAround) {
      RYGLog(RYGLogLevelDebug, "justifyContent: 'space-around', ");
    } else if (node->style.justifyContent == RYGJustifySpaceBetween) {
      RYGLog(RYGLogLevelDebug, "justifyContent: 'space-between', ");
    }

    if (node->style.alignItems == RYGAlignCenter) {
      RYGLog(RYGLogLevelDebug, "alignItems: 'center', ");
    } else if (node->style.alignItems == RYGAlignFlexEnd) {
      RYGLog(RYGLogLevelDebug, "alignItems: 'flex-end', ");
    } else if (node->style.alignItems == RYGAlignStretch) {
      RYGLog(RYGLogLevelDebug, "alignItems: 'stretch', ");
    }

    if (node->style.alignContent == RYGAlignCenter) {
      RYGLog(RYGLogLevelDebug, "alignContent: 'center', ");
    } else if (node->style.alignContent == RYGAlignFlexEnd) {
      RYGLog(RYGLogLevelDebug, "alignContent: 'flex-end', ");
    } else if (node->style.alignContent == RYGAlignStretch) {
      RYGLog(RYGLogLevelDebug, "alignContent: 'stretch', ");
    }

    if (node->style.alignSelf == RYGAlignFlexStart) {
      RYGLog(RYGLogLevelDebug, "alignSelf: 'flex-start', ");
    } else if (node->style.alignSelf == RYGAlignCenter) {
      RYGLog(RYGLogLevelDebug, "alignSelf: 'center', ");
    } else if (node->style.alignSelf == RYGAlignFlexEnd) {
      RYGLog(RYGLogLevelDebug, "alignSelf: 'flex-end', ");
    } else if (node->style.alignSelf == RYGAlignStretch) {
      RYGLog(RYGLogLevelDebug, "alignSelf: 'stretch', ");
    }

    RYGPrintNumberIfNotUndefinedf("flexGrow", RYGNodeStyleGetFlexGrow(node));
    RYGPrintNumberIfNotUndefinedf("flexShrink", RYGNodeStyleGetFlexShrink(node));
    RYGPrintNumberIfNotUndefined("flexBasis", RYGNodeStyleGetFlexBasisPtr(node));

    if (node->style.overflow == RYGOverflowHidden) {
      RYGLog(RYGLogLevelDebug, "overflow: 'hidden', ");
    } else if (node->style.overflow == RYGOverflowVisible) {
      RYGLog(RYGLogLevelDebug, "overflow: 'visible', ");
    } else if (node->style.overflow == RYGOverflowScroll) {
      RYGLog(RYGLogLevelDebug, "overflow: 'scroll', ");
    }

    if (RYGFourValuesEqual(node->style.margin)) {
      RYGPrintNumberIfNotZero("margin",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeLeft, &RYGValueZero));
    } else {
      RYGPrintNumberIfNotZero("marginLeft",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeLeft, &RYGValueZero));
      RYGPrintNumberIfNotZero("marginRight",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeRight, &RYGValueZero));
      RYGPrintNumberIfNotZero("marginTop",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeTop, &RYGValueZero));
      RYGPrintNumberIfNotZero("marginBottom",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeBottom, &RYGValueZero));
      RYGPrintNumberIfNotZero("marginStart",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeStart, &RYGValueZero));
      RYGPrintNumberIfNotZero("marginEnd",
                             RYGComputedEdgeValue(node->style.margin, RYGEdgeEnd, &RYGValueZero));
    }

    if (RYGFourValuesEqual(node->style.padding)) {
      RYGPrintNumberIfNotZero("padding",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeLeft, &RYGValueZero));
    } else {
      RYGPrintNumberIfNotZero("paddingLeft",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeLeft, &RYGValueZero));
      RYGPrintNumberIfNotZero("paddingRight",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeRight, &RYGValueZero));
      RYGPrintNumberIfNotZero("paddingTop",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeTop, &RYGValueZero));
      RYGPrintNumberIfNotZero("paddingBottom",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeBottom, &RYGValueZero));
      RYGPrintNumberIfNotZero("paddingStart",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeStart, &RYGValueZero));
      RYGPrintNumberIfNotZero("paddingEnd",
                             RYGComputedEdgeValue(node->style.padding, RYGEdgeEnd, &RYGValueZero));
    }

    if (RYGFourValuesEqual(node->style.border)) {
      RYGPrintNumberIfNotZero("borderWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeLeft, &RYGValueZero));
    } else {
      RYGPrintNumberIfNotZero("borderLeftWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeLeft, &RYGValueZero));
      RYGPrintNumberIfNotZero("borderRightWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeRight, &RYGValueZero));
      RYGPrintNumberIfNotZero("borderTopWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeTop, &RYGValueZero));
      RYGPrintNumberIfNotZero("borderBottomWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeBottom, &RYGValueZero));
      RYGPrintNumberIfNotZero("borderStartWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeStart, &RYGValueZero));
      RYGPrintNumberIfNotZero("borderEndWidth",
                             RYGComputedEdgeValue(node->style.border, RYGEdgeEnd, &RYGValueZero));
    }

    RYGPrintNumberIfNotUndefined("width", &node->style.dimensions[RYGDimensionWidth]);
    RYGPrintNumberIfNotUndefined("height", &node->style.dimensions[RYGDimensionHeight]);
    RYGPrintNumberIfNotUndefined("maxWidth", &node->style.maxDimensions[RYGDimensionWidth]);
    RYGPrintNumberIfNotUndefined("maxHeight", &node->style.maxDimensions[RYGDimensionHeight]);
    RYGPrintNumberIfNotUndefined("minWidth", &node->style.minDimensions[RYGDimensionWidth]);
    RYGPrintNumberIfNotUndefined("minHeight", &node->style.minDimensions[RYGDimensionHeight]);

    if (node->style.positionType == RYGPositionTypeAbsolute) {
      RYGLog(RYGLogLevelDebug, "position: 'absolute', ");
    }

    RYGPrintNumberIfNotUndefined(
                                "left", RYGComputedEdgeValue(node->style.position, RYGEdgeLeft, &RYGValueUndefined));
    RYGPrintNumberIfNotUndefined(
                                "right", RYGComputedEdgeValue(node->style.position, RYGEdgeRight, &RYGValueUndefined));
    RYGPrintNumberIfNotUndefined(
                                "top", RYGComputedEdgeValue(node->style.position, RYGEdgeTop, &RYGValueUndefined));
    RYGPrintNumberIfNotUndefined(
                                "bottom", RYGComputedEdgeValue(node->style.position, RYGEdgeBottom, &RYGValueUndefined));
  }

  const uint32_t childCount = RYGNodeListCount(node->children);
  if (options & RYGPrintOptionsChildren && childCount > 0) {
    RYGLog(RYGLogLevelDebug, "children: [\n");
    for (uint32_t i = 0; i < childCount; i++) {
      RYGNodePrintInternal(RYGNodeGetChild(node, i), options, level + 1);
    }
    RYGIndent(level);
    RYGLog(RYGLogLevelDebug, "]},\n");
  } else {
    RYGLog(RYGLogLevelDebug, "},\n");
  }
}

void RYGNodePrint(const RYGNodeRef node, const RYGPrintOptions options) {
  RYGNodePrintInternal(node, options, 0);
}

static const RYGEdge leading[4] = {
  [RYGFlexDirectionColumn] = RYGEdgeTop,
  [RYGFlexDirectionColumnReverse] = RYGEdgeBottom,
  [RYGFlexDirectionRow] = RYGEdgeLeft,
  [RYGFlexDirectionRowReverse] = RYGEdgeRight,
};
static const RYGEdge trailing[4] = {
  [RYGFlexDirectionColumn] = RYGEdgeBottom,
  [RYGFlexDirectionColumnReverse] = RYGEdgeTop,
  [RYGFlexDirectionRow] = RYGEdgeRight,
  [RYGFlexDirectionRowReverse] = RYGEdgeLeft,
};
static const RYGEdge pos[4] = {
  [RYGFlexDirectionColumn] = RYGEdgeTop,
  [RYGFlexDirectionColumnReverse] = RYGEdgeBottom,
  [RYGFlexDirectionRow] = RYGEdgeLeft,
  [RYGFlexDirectionRowReverse] = RYGEdgeRight,
};
static const RYGDimension dim[4] = {
  [RYGFlexDirectionColumn] = RYGDimensionHeight,
  [RYGFlexDirectionColumnReverse] = RYGDimensionHeight,
  [RYGFlexDirectionRow] = RYGDimensionWidth,
  [RYGFlexDirectionRowReverse] = RYGDimensionWidth,
};

static inline bool RYGFlexDirectionIsRow(const RYGFlexDirection flexDirection) {
  return flexDirection == RYGFlexDirectionRow || flexDirection == RYGFlexDirectionRowReverse;
}

static inline bool RYGFlexDirectionIsColumn(const RYGFlexDirection flexDirection) {
  return flexDirection == RYGFlexDirectionColumn || flexDirection == RYGFlexDirectionColumnReverse;
}

static inline float RYGNodeLeadingMargin(const RYGNodeRef node,
                                        const RYGFlexDirection axis,
                                        const float widthSize) {
  if (RYGFlexDirectionIsRow(axis) && node->style.margin[RYGEdgeStart].unit != RYGUnitUndefined) {
    return RYGValueResolveMargin(&node->style.margin[RYGEdgeStart], widthSize);
  }

  return RYGValueResolveMargin(RYGComputedEdgeValue(node->style.margin, leading[axis], &RYGValueZero),
                              widthSize);
}

static float RYGNodeTrailingMargin(const RYGNodeRef node,
                                  const RYGFlexDirection axis,
                                  const float widthSize) {
  if (RYGFlexDirectionIsRow(axis) && node->style.margin[RYGEdgeEnd].unit != RYGUnitUndefined) {
    return RYGValueResolveMargin(&node->style.margin[RYGEdgeEnd], widthSize);
  }

  return RYGValueResolveMargin(RYGComputedEdgeValue(node->style.margin, trailing[axis], &RYGValueZero),
                              widthSize);
}

static float RYGNodeLeadingPadding(const RYGNodeRef node,
                                  const RYGFlexDirection axis,
                                  const float widthSize) {
  if (RYGFlexDirectionIsRow(axis) && node->style.padding[RYGEdgeStart].unit != RYGUnitUndefined &&
      RYGValueResolve(&node->style.padding[RYGEdgeStart], widthSize) >= 0.0f) {
    return RYGValueResolve(&node->style.padding[RYGEdgeStart], widthSize);
  }

  return fmaxf(RYGValueResolve(RYGComputedEdgeValue(node->style.padding, leading[axis], &RYGValueZero),
                              widthSize),
               0.0f);
}

static float RYGNodeTrailingPadding(const RYGNodeRef node,
                                   const RYGFlexDirection axis,
                                   const float widthSize) {
  if (RYGFlexDirectionIsRow(axis) && node->style.padding[RYGEdgeEnd].unit != RYGUnitUndefined &&
      RYGValueResolve(&node->style.padding[RYGEdgeEnd], widthSize) >= 0.0f) {
    return RYGValueResolve(&node->style.padding[RYGEdgeEnd], widthSize);
  }

  return fmaxf(RYGValueResolve(RYGComputedEdgeValue(node->style.padding, trailing[axis], &RYGValueZero),
                              widthSize),
               0.0f);
}

static float RYGNodeLeadingBorder(const RYGNodeRef node, const RYGFlexDirection axis) {
  if (RYGFlexDirectionIsRow(axis) && node->style.border[RYGEdgeStart].unit != RYGUnitUndefined &&
      node->style.border[RYGEdgeStart].value >= 0.0f) {
    return node->style.border[RYGEdgeStart].value;
  }

  return fmaxf(RYGComputedEdgeValue(node->style.border, leading[axis], &RYGValueZero)->value, 0.0f);
}

static float RYGNodeTrailingBorder(const RYGNodeRef node, const RYGFlexDirection axis) {
  if (RYGFlexDirectionIsRow(axis) && node->style.border[RYGEdgeEnd].unit != RYGUnitUndefined &&
      node->style.border[RYGEdgeEnd].value >= 0.0f) {
    return node->style.border[RYGEdgeEnd].value;
  }

  return fmaxf(RYGComputedEdgeValue(node->style.border, trailing[axis], &RYGValueZero)->value, 0.0f);
}

static inline float RYGNodeLeadingPaddingAndBorder(const RYGNodeRef node,
                                                  const RYGFlexDirection axis,
                                                  const float widthSize) {
  return RYGNodeLeadingPadding(node, axis, widthSize) + RYGNodeLeadingBorder(node, axis);
}

static inline float RYGNodeTrailingPaddingAndBorder(const RYGNodeRef node,
                                                   const RYGFlexDirection axis,
                                                   const float widthSize) {
  return RYGNodeTrailingPadding(node, axis, widthSize) + RYGNodeTrailingBorder(node, axis);
}

static inline float RYGNodeMarginForAxis(const RYGNodeRef node,
                                        const RYGFlexDirection axis,
                                        const float widthSize) {
  return RYGNodeLeadingMargin(node, axis, widthSize) + RYGNodeTrailingMargin(node, axis, widthSize);
}

static inline float RYGNodePaddingAndBorderForAxis(const RYGNodeRef node,
                                                  const RYGFlexDirection axis,
                                                  const float widthSize) {
  return RYGNodeLeadingPaddingAndBorder(node, axis, widthSize) +
  RYGNodeTrailingPaddingAndBorder(node, axis, widthSize);
}

static inline RYGAlign RYGNodeAlignItem(const RYGNodeRef node, const RYGNodeRef child) {
  const RYGAlign align =
  child->style.alignSelf == RYGAlignAuto ? node->style.alignItems : child->style.alignSelf;
  if (align == RYGAlignBaseline && RYGFlexDirectionIsColumn(node->style.flexDirection)) {
    return RYGAlignFlexStart;
  }
  return align;
}

static inline RYGDirection RYGNodeResolveDirection(const RYGNodeRef node,
                                                 const RYGDirection parentDirection) {
  if (node->style.direction == RYGDirectionInherit) {
    return parentDirection > RYGDirectionInherit ? parentDirection : RYGDirectionLTR;
  } else {
    return node->style.direction;
  }
}

static float RYGBaseline(const RYGNodeRef node) {
  if (node->baseline != NULL) {
    const float baseline = node->baseline(node,
                                          node->layout.measuredDimensions[RYGDimensionWidth],
                                          node->layout.measuredDimensions[RYGDimensionHeight]);
    RYG_ASSERT(!RYGFloatIsUndefined(baseline), "Expect custom baseline function to not return NaN")
    return baseline;
  }

  RYGNodeRef baselineChild = NULL;
  const uint32_t childCount = RYGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; i++) {
    const RYGNodeRef child = RYGNodeGetChild(node, i);
    if (child->lineIndex > 0) {
      break;
    }
    if (child->style.positionType == RYGPositionTypeAbsolute) {
      continue;
    }
    if (RYGNodeAlignItem(node, child) == RYGAlignBaseline) {
      baselineChild = child;
      break;
    }

    if (baselineChild == NULL) {
      baselineChild = child;
    }
  }

  if (baselineChild == NULL) {
    return node->layout.measuredDimensions[RYGDimensionHeight];
  }

  const float baseline = RYGBaseline(baselineChild);
  return baseline + baselineChild->layout.position[RYGEdgeTop];
}

static inline RYGFlexDirection RYGFlexDirectionResolve(const RYGFlexDirection flexDirection,
                                                     const RYGDirection direction) {
  if (direction == RYGDirectionRTL) {
    if (flexDirection == RYGFlexDirectionRow) {
      return RYGFlexDirectionRowReverse;
    } else if (flexDirection == RYGFlexDirectionRowReverse) {
      return RYGFlexDirectionRow;
    }
  }

  return flexDirection;
}

static RYGFlexDirection RYGFlexDirectionCross(const RYGFlexDirection flexDirection,
                                            const RYGDirection direction) {
  return RYGFlexDirectionIsColumn(flexDirection)
  ? RYGFlexDirectionResolve(RYGFlexDirectionRow, direction)
  : RYGFlexDirectionColumn;
}

static inline bool RYGNodeIsFlex(const RYGNodeRef node) {
  return (node->style.positionType == RYGPositionTypeRelative &&
          (RYGNodeStyleGetFlexGrow(node) != 0 || RYGNodeStyleGetFlexShrink(node) != 0));
}

static bool RYGIsBaselineLayout(const RYGNodeRef node) {
  if (RYGFlexDirectionIsColumn(node->style.flexDirection)) {
    return false;
  }
  if (node->style.alignItems == RYGAlignBaseline) {
    return true;
  }
  const uint32_t childCount = RYGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; i++) {
    const RYGNodeRef child = RYGNodeGetChild(node, i);
    if (child->style.positionType == RYGPositionTypeRelative &&
        child->style.alignSelf == RYGAlignBaseline) {
      return true;
    }
  }

  return false;
}

static inline float RYGNodeDimWithMargin(const RYGNodeRef node,
                                        const RYGFlexDirection axis,
                                        const float widthSize) {
  return node->layout.measuredDimensions[dim[axis]] + RYGNodeLeadingMargin(node, axis, widthSize) +
  RYGNodeTrailingMargin(node, axis, widthSize);
}

static inline bool RYGNodeIsStyleDimDefined(const RYGNodeRef node,
                                           const RYGFlexDirection axis,
                                           const float parentSize) {
  return !(node->resolvedDimensions[dim[axis]]->unit == RYGUnitAuto ||
           node->resolvedDimensions[dim[axis]]->unit == RYGUnitUndefined ||
           (node->resolvedDimensions[dim[axis]]->unit == RYGUnitPoint &&
            node->resolvedDimensions[dim[axis]]->value < 0.0f) ||
           (node->resolvedDimensions[dim[axis]]->unit == RYGUnitPercent &&
            (node->resolvedDimensions[dim[axis]]->value < 0.0f || RYGFloatIsUndefined(parentSize))));
}

static inline bool RYGNodeIsLayoutDimDefined(const RYGNodeRef node, const RYGFlexDirection axis) {
  const float value = node->layout.measuredDimensions[dim[axis]];
  return !RYGFloatIsUndefined(value) && value >= 0.0f;
}

static inline bool RYGNodeIsLeadingPosDefined(const RYGNodeRef node, const RYGFlexDirection axis) {
  return (RYGFlexDirectionIsRow(axis) &&
          RYGComputedEdgeValue(node->style.position, RYGEdgeStart, &RYGValueUndefined)->unit !=
          RYGUnitUndefined) ||
  RYGComputedEdgeValue(node->style.position, leading[axis], &RYGValueUndefined)->unit !=
  RYGUnitUndefined;
}

static inline bool RYGNodeIsTrailingPosDefined(const RYGNodeRef node, const RYGFlexDirection axis) {
  return (RYGFlexDirectionIsRow(axis) &&
          RYGComputedEdgeValue(node->style.position, RYGEdgeEnd, &RYGValueUndefined)->unit !=
          RYGUnitUndefined) ||
  RYGComputedEdgeValue(node->style.position, trailing[axis], &RYGValueUndefined)->unit !=
  RYGUnitUndefined;
}

static float RYGNodeLeadingPosition(const RYGNodeRef node,
                                   const RYGFlexDirection axis,
                                   const float axisSize) {
  if (RYGFlexDirectionIsRow(axis)) {
    const RYGValue *leadingPosition =
    RYGComputedEdgeValue(node->style.position, RYGEdgeStart, &RYGValueUndefined);
    if (leadingPosition->unit != RYGUnitUndefined) {
      return RYGValueResolve(leadingPosition, axisSize);
    }
  }

  const RYGValue *leadingPosition =
  RYGComputedEdgeValue(node->style.position, leading[axis], &RYGValueUndefined);

  return leadingPosition->unit == RYGUnitUndefined ? 0.0f
  : RYGValueResolve(leadingPosition, axisSize);
}

static float RYGNodeTrailingPosition(const RYGNodeRef node,
                                    const RYGFlexDirection axis,
                                    const float axisSize) {
  if (RYGFlexDirectionIsRow(axis)) {
    const RYGValue *trailingPosition =
    RYGComputedEdgeValue(node->style.position, RYGEdgeEnd, &RYGValueUndefined);
    if (trailingPosition->unit != RYGUnitUndefined) {
      return RYGValueResolve(trailingPosition, axisSize);
    }
  }

  const RYGValue *trailingPosition =
  RYGComputedEdgeValue(node->style.position, trailing[axis], &RYGValueUndefined);

  return trailingPosition->unit == RYGUnitUndefined ? 0.0f
  : RYGValueResolve(trailingPosition, axisSize);
}

static float RYGNodeBoundAxisWithinMinAndMax(const RYGNodeRef node,
                                            const RYGFlexDirection axis,
                                            const float value,
                                            const float axisSize) {
  float min = RYGUndefined;
  float max = RYGUndefined;

  if (RYGFlexDirectionIsColumn(axis)) {
    min = RYGValueResolve(&node->style.minDimensions[RYGDimensionHeight], axisSize);
    max = RYGValueResolve(&node->style.maxDimensions[RYGDimensionHeight], axisSize);
  } else if (RYGFlexDirectionIsRow(axis)) {
    min = RYGValueResolve(&node->style.minDimensions[RYGDimensionWidth], axisSize);
    max = RYGValueResolve(&node->style.maxDimensions[RYGDimensionWidth], axisSize);
  }

  float boundValue = value;

  if (!RYGFloatIsUndefined(max) && max >= 0.0f && boundValue > max) {
    boundValue = max;
  }

  if (!RYGFloatIsUndefined(min) && min >= 0.0f && boundValue < min) {
    boundValue = min;
  }

  return boundValue;
}

// Like RYGNodeBoundAxisWithinMinAndMax but also ensures that the value doesn't go
// below the
// padding and border amount.
static inline float RYGNodeBoundAxis(const RYGNodeRef node,
                                    const RYGFlexDirection axis,
                                    const float value,
                                    const float axisSize,
                                    const float widthSize) {
  return fmaxf(RYGNodeBoundAxisWithinMinAndMax(node, axis, value, axisSize),
               RYGNodePaddingAndBorderForAxis(node, axis, widthSize));
}

static void RYGNodeSetChildTrailingPosition(const RYGNodeRef node,
                                           const RYGNodeRef child,
                                           const RYGFlexDirection axis) {
  const float size = child->layout.measuredDimensions[dim[axis]];
  child->layout.position[trailing[axis]] =
  node->layout.measuredDimensions[dim[axis]] - size - child->layout.position[pos[axis]];
}

// If both left and right are defined, then use left. Otherwise return
// +left or -right depending on which is defined.
static float RYGNodeRelativePosition(const RYGNodeRef node,
                                    const RYGFlexDirection axis,
                                    const float axisSize) {
  return RYGNodeIsLeadingPosDefined(node, axis) ? RYGNodeLeadingPosition(node, axis, axisSize)
  : -RYGNodeTrailingPosition(node, axis, axisSize);
}

static void RYGConstrainMaxSizeForMode(const float maxSize, RYGMeasureMode *mode, float *size) {
  switch (*mode) {
    case RYGMeasureModeExactly:
    case RYGMeasureModeAtMost:
      *size = (RYGFloatIsUndefined(maxSize) || *size < maxSize) ? *size : maxSize;
      break;
    case RYGMeasureModeUndefined:
      if (!RYGFloatIsUndefined(maxSize)) {
        *mode = RYGMeasureModeAtMost;
        *size = maxSize;
      }
      break;
  }
}

static void RYGNodeSetPosition(const RYGNodeRef node,
                              const RYGDirection direction,
                              const float mainSize,
                              const float crossSize,
                              const float parentWidth) {
  const RYGFlexDirection mainAxis = RYGFlexDirectionResolve(node->style.flexDirection, direction);
  const RYGFlexDirection crossAxis = RYGFlexDirectionCross(mainAxis, direction);
  const float relativePositionMain = RYGNodeRelativePosition(node, mainAxis, mainSize);
  const float relativePositionCross = RYGNodeRelativePosition(node, crossAxis, crossSize);

  node->layout.position[leading[mainAxis]] =
  RYGNodeLeadingMargin(node, mainAxis, parentWidth) + relativePositionMain;
  node->layout.position[trailing[mainAxis]] =
  RYGNodeTrailingMargin(node, mainAxis, parentWidth) + relativePositionMain;
  node->layout.position[leading[crossAxis]] =
  RYGNodeLeadingMargin(node, crossAxis, parentWidth) + relativePositionCross;
  node->layout.position[trailing[crossAxis]] =
  RYGNodeTrailingMargin(node, crossAxis, parentWidth) + relativePositionCross;
}

static void RYGNodeComputeFlexBasisForChild(const RYGNodeRef node,
                                           const RYGNodeRef child,
                                           const float width,
                                           const RYGMeasureMode widthMode,
                                           const float height,
                                           const float parentWidth,
                                           const float parentHeight,
                                           const RYGMeasureMode heightMode,
                                           const RYGDirection direction) {
  const RYGFlexDirection mainAxis = RYGFlexDirectionResolve(node->style.flexDirection, direction);
  const bool isMainAxisRow = RYGFlexDirectionIsRow(mainAxis);
  const float mainAxisSize = isMainAxisRow ? width : height;
  const float mainAxisParentSize = isMainAxisRow ? parentWidth : parentHeight;

  float childWidth;
  float childHeight;
  RYGMeasureMode childWidthMeasureMode;
  RYGMeasureMode childHeightMeasureMode;

  const float resolvedFlexBasis =
  RYGValueResolve(RYGNodeStyleGetFlexBasisPtr(child), mainAxisParentSize);
  const bool isRowStyleDimDefined = RYGNodeIsStyleDimDefined(child, RYGFlexDirectionRow, parentWidth);
  const bool isColumnStyleDimDefined =
  RYGNodeIsStyleDimDefined(child, RYGFlexDirectionColumn, parentHeight);

  if (!RYGFloatIsUndefined(resolvedFlexBasis) && !RYGFloatIsUndefined(mainAxisSize)) {
    if (RYGFloatIsUndefined(child->layout.computedFlexBasis) ||
        (RYGIsExperimentalFeatureEnabled(RYGExperimentalFeatureWebFlexBasis) &&
         child->layout.computedFlexBasisGeneration != gCurrentGenerationCount)) {
          child->layout.computedFlexBasis =
          fmaxf(resolvedFlexBasis, RYGNodePaddingAndBorderForAxis(child, mainAxis, parentWidth));
        }
  } else if (isMainAxisRow && isRowStyleDimDefined) {
    // The width is definite, so use that as the flex basis.
    child->layout.computedFlexBasis =
    fmaxf(RYGValueResolve(child->resolvedDimensions[RYGDimensionWidth], parentWidth),
          RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionRow, parentWidth));
  } else if (!isMainAxisRow && isColumnStyleDimDefined) {
    // The height is definite, so use that as the flex basis.
    child->layout.computedFlexBasis =
    fmaxf(RYGValueResolve(child->resolvedDimensions[RYGDimensionHeight], parentHeight),
          RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionColumn, parentWidth));
  } else {
    // Compute the flex basis and hypothetical main size (i.e. the clamped
    // flex basis).
    childWidth = RYGUndefined;
    childHeight = RYGUndefined;
    childWidthMeasureMode = RYGMeasureModeUndefined;
    childHeightMeasureMode = RYGMeasureModeUndefined;

    const float marginRow = RYGNodeMarginForAxis(child, RYGFlexDirectionRow, parentWidth);
    const float marginColumn = RYGNodeMarginForAxis(child, RYGFlexDirectionColumn, parentWidth);

    if (isRowStyleDimDefined) {
      childWidth =
      RYGValueResolve(child->resolvedDimensions[RYGDimensionWidth], parentWidth) + marginRow;
      childWidthMeasureMode = RYGMeasureModeExactly;
    }
    if (isColumnStyleDimDefined) {
      childHeight =
      RYGValueResolve(child->resolvedDimensions[RYGDimensionHeight], parentHeight) + marginColumn;
      childHeightMeasureMode = RYGMeasureModeExactly;
    }

    // The W3C spec doesn't say anything about the 'overflow' property,
    // but all major browsers appear to implement the following logic.
    if ((!isMainAxisRow && node->style.overflow == RYGOverflowScroll) ||
        node->style.overflow != RYGOverflowScroll) {
      if (RYGFloatIsUndefined(childWidth) && !RYGFloatIsUndefined(width)) {
        childWidth = width;
        childWidthMeasureMode = RYGMeasureModeAtMost;
      }
    }

    if ((isMainAxisRow && node->style.overflow == RYGOverflowScroll) ||
        node->style.overflow != RYGOverflowScroll) {
      if (RYGFloatIsUndefined(childHeight) && !RYGFloatIsUndefined(height)) {
        childHeight = height;
        childHeightMeasureMode = RYGMeasureModeAtMost;
      }
    }

    // If child has no defined size in the cross axis and is set to stretch,
    // set the cross
    // axis to be measured exactly with the available inner width
    if (!isMainAxisRow && !RYGFloatIsUndefined(width) && !isRowStyleDimDefined &&
        widthMode == RYGMeasureModeExactly && RYGNodeAlignItem(node, child) == RYGAlignStretch) {
      childWidth = width;
      childWidthMeasureMode = RYGMeasureModeExactly;
    }
    if (isMainAxisRow && !RYGFloatIsUndefined(height) && !isColumnStyleDimDefined &&
        heightMode == RYGMeasureModeExactly && RYGNodeAlignItem(node, child) == RYGAlignStretch) {
      childHeight = height;
      childHeightMeasureMode = RYGMeasureModeExactly;
    }

    if (!RYGFloatIsUndefined(child->style.aspectRatio)) {
      if (!isMainAxisRow && childWidthMeasureMode == RYGMeasureModeExactly) {
        child->layout.computedFlexBasis =
        fmaxf((childWidth - marginRow) / child->style.aspectRatio,
              RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionColumn, parentWidth));
        return;
      } else if (isMainAxisRow && childHeightMeasureMode == RYGMeasureModeExactly) {
        child->layout.computedFlexBasis =
        fmaxf((childHeight - marginColumn) * child->style.aspectRatio,
              RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionRow, parentWidth));
        return;
      }
    }

    RYGConstrainMaxSizeForMode(RYGValueResolve(&child->style.maxDimensions[RYGDimensionWidth],
                                             parentWidth),
                              &childWidthMeasureMode,
                              &childWidth);
    RYGConstrainMaxSizeForMode(RYGValueResolve(&child->style.maxDimensions[RYGDimensionHeight],
                                             parentHeight),
                              &childHeightMeasureMode,
                              &childHeight);

    // Measure the child
    RYGLayoutNodeInternal(child,
                         childWidth,
                         childHeight,
                         direction,
                         childWidthMeasureMode,
                         childHeightMeasureMode,
                         parentWidth,
                         parentHeight,
                         false,
                         "measure");

    child->layout.computedFlexBasis =
    fmaxf(child->layout.measuredDimensions[dim[mainAxis]],
          RYGNodePaddingAndBorderForAxis(child, mainAxis, parentWidth));
  }

  child->layout.computedFlexBasisGeneration = gCurrentGenerationCount;
}

static void RYGNodeAbsoluteLayoutChild(const RYGNodeRef node,
                                      const RYGNodeRef child,
                                      const float width,
                                      const RYGMeasureMode widthMode,
                                      const float height,
                                      const RYGDirection direction) {
  const RYGFlexDirection mainAxis = RYGFlexDirectionResolve(node->style.flexDirection, direction);
  const RYGFlexDirection crossAxis = RYGFlexDirectionCross(mainAxis, direction);
  const bool isMainAxisRow = RYGFlexDirectionIsRow(mainAxis);

  float childWidth = RYGUndefined;
  float childHeight = RYGUndefined;
  RYGMeasureMode childWidthMeasureMode = RYGMeasureModeUndefined;
  RYGMeasureMode childHeightMeasureMode = RYGMeasureModeUndefined;

  const float marginRow = RYGNodeMarginForAxis(child, RYGFlexDirectionRow, width);
  const float marginColumn = RYGNodeMarginForAxis(child, RYGFlexDirectionColumn, width);

  if (RYGNodeIsStyleDimDefined(child, RYGFlexDirectionRow, width)) {
    childWidth = RYGValueResolve(child->resolvedDimensions[RYGDimensionWidth], width) + marginRow;
  } else {
    // If the child doesn't have a specified width, compute the width based
    // on the left/right
    // offsets if they're defined.
    if (RYGNodeIsLeadingPosDefined(child, RYGFlexDirectionRow) &&
        RYGNodeIsTrailingPosDefined(child, RYGFlexDirectionRow)) {
      childWidth = node->layout.measuredDimensions[RYGDimensionWidth] -
      (RYGNodeLeadingBorder(node, RYGFlexDirectionRow) +
       RYGNodeTrailingBorder(node, RYGFlexDirectionRow)) -
      (RYGNodeLeadingPosition(child, RYGFlexDirectionRow, width) +
       RYGNodeTrailingPosition(child, RYGFlexDirectionRow, width));
      childWidth = RYGNodeBoundAxis(child, RYGFlexDirectionRow, childWidth, width, width);
    }
  }

  if (RYGNodeIsStyleDimDefined(child, RYGFlexDirectionColumn, height)) {
    childHeight =
    RYGValueResolve(child->resolvedDimensions[RYGDimensionHeight], height) + marginColumn;
  } else {
    // If the child doesn't have a specified height, compute the height
    // based on the top/bottom
    // offsets if they're defined.
    if (RYGNodeIsLeadingPosDefined(child, RYGFlexDirectionColumn) &&
        RYGNodeIsTrailingPosDefined(child, RYGFlexDirectionColumn)) {
      childHeight = node->layout.measuredDimensions[RYGDimensionHeight] -
      (RYGNodeLeadingBorder(node, RYGFlexDirectionColumn) +
       RYGNodeTrailingBorder(node, RYGFlexDirectionColumn)) -
      (RYGNodeLeadingPosition(child, RYGFlexDirectionColumn, height) +
       RYGNodeTrailingPosition(child, RYGFlexDirectionColumn, height));
      childHeight = RYGNodeBoundAxis(child, RYGFlexDirectionColumn, childHeight, height, width);
    }
  }

  // Exactly one dimension needs to be defined for us to be able to do aspect ratio
  // calculation. One dimension being the anchor and the other being flexible.
  if (RYGFloatIsUndefined(childWidth) ^ RYGFloatIsUndefined(childHeight)) {
    if (!RYGFloatIsUndefined(child->style.aspectRatio)) {
      if (RYGFloatIsUndefined(childWidth)) {
        childWidth =
        marginRow + fmaxf((childHeight - marginColumn) * child->style.aspectRatio,
                          RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionColumn, width));
      } else if (RYGFloatIsUndefined(childHeight)) {
        childHeight =
        marginColumn + fmaxf((childWidth - marginRow) / child->style.aspectRatio,
                             RYGNodePaddingAndBorderForAxis(child, RYGFlexDirectionRow, width));
      }
    }
  }

  // If we're still missing one or the other dimension, measure the content.
  if (RYGFloatIsUndefined(childWidth) || RYGFloatIsUndefined(childHeight)) {
    childWidthMeasureMode =
    RYGFloatIsUndefined(childWidth) ? RYGMeasureModeUndefined : RYGMeasureModeExactly;
    childHeightMeasureMode =
    RYGFloatIsUndefined(childHeight) ? RYGMeasureModeUndefined : RYGMeasureModeExactly;

    // If the size of the parent is defined then try to constrain the absolute child to that size
    // as well. This allows text within the absolute child to wrap to the size of its parent.
    // This is the same behavior as many browsers implement.
    if (!isMainAxisRow && RYGFloatIsUndefined(childWidth) && widthMode != RYGMeasureModeUndefined &&
        width > 0) {
      childWidth = width;
      childWidthMeasureMode = RYGMeasureModeAtMost;
    }

    RYGLayoutNodeInternal(child,
                         childWidth,
                         childHeight,
                         direction,
                         childWidthMeasureMode,
                         childHeightMeasureMode,
                         childWidth,
                         childHeight,
                         false,
                         "abs-measure");
    childWidth = child->layout.measuredDimensions[RYGDimensionWidth] +
    RYGNodeMarginForAxis(child, RYGFlexDirectionRow, width);
    childHeight = child->layout.measuredDimensions[RYGDimensionHeight] +
    RYGNodeMarginForAxis(child, RYGFlexDirectionColumn, width);
  }

  RYGLayoutNodeInternal(child,
                       childWidth,
                       childHeight,
                       direction,
                       RYGMeasureModeExactly,
                       RYGMeasureModeExactly,
                       childWidth,
                       childHeight,
                       true,
                       "abs-layout");

  if (RYGNodeIsTrailingPosDefined(child, mainAxis) && !RYGNodeIsLeadingPosDefined(child, mainAxis)) {
    child->layout.position[leading[mainAxis]] = node->layout.measuredDimensions[dim[mainAxis]] -
    child->layout.measuredDimensions[dim[mainAxis]] -
    RYGNodeTrailingBorder(node, mainAxis) -
    RYGNodeTrailingPosition(child, mainAxis, width);
  } else if (!RYGNodeIsLeadingPosDefined(child, mainAxis) &&
             node->style.justifyContent == RYGJustifyCenter) {
    child->layout.position[leading[mainAxis]] = (node->layout.measuredDimensions[dim[mainAxis]] -
                                                 child->layout.measuredDimensions[dim[mainAxis]]) /
    2.0f;
  } else if (!RYGNodeIsLeadingPosDefined(child, mainAxis) &&
             node->style.justifyContent == RYGJustifyFlexEnd) {
    child->layout.position[leading[mainAxis]] = (node->layout.measuredDimensions[dim[mainAxis]] -
                                                 child->layout.measuredDimensions[dim[mainAxis]]);
  }

  if (RYGNodeIsTrailingPosDefined(child, crossAxis) &&
      !RYGNodeIsLeadingPosDefined(child, crossAxis)) {
    child->layout.position[leading[crossAxis]] = node->layout.measuredDimensions[dim[crossAxis]] -
    child->layout.measuredDimensions[dim[crossAxis]] -
    RYGNodeTrailingBorder(node, crossAxis) -
    RYGNodeTrailingPosition(child, crossAxis, width);
  } else if (!RYGNodeIsLeadingPosDefined(child, crossAxis) &&
             RYGNodeAlignItem(node, child) == RYGAlignCenter) {
    child->layout.position[leading[crossAxis]] =
    (node->layout.measuredDimensions[dim[crossAxis]] -
     child->layout.measuredDimensions[dim[crossAxis]]) /
    2.0f;
  } else if (!RYGNodeIsLeadingPosDefined(child, crossAxis) &&
             RYGNodeAlignItem(node, child) == RYGAlignFlexEnd) {
    child->layout.position[leading[crossAxis]] = (node->layout.measuredDimensions[dim[crossAxis]] -
                                                  child->layout.measuredDimensions[dim[crossAxis]]);
  }
}

static void RYGNodeWithMeasureFuncSetMeasuredDimensions(const RYGNodeRef node,
                                                       const float availableWidth,
                                                       const float availableHeight,
                                                       const RYGMeasureMode widthMeasureMode,
                                                       const RYGMeasureMode heightMeasureMode,
                                                       const float parentWidth,
                                                       const float parentHeight) {
  RYG_ASSERT(node->measure, "Expected node to have custom measure function");

  const float paddingAndBorderAxisRow =
  RYGNodePaddingAndBorderForAxis(node, RYGFlexDirectionRow, availableWidth);
  const float paddingAndBorderAxisColumn =
  RYGNodePaddingAndBorderForAxis(node, RYGFlexDirectionColumn, availableWidth);
  const float marginAxisRow = RYGNodeMarginForAxis(node, RYGFlexDirectionRow, availableWidth);
  const float marginAxisColumn = RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, availableWidth);

  const float innerWidth = availableWidth - marginAxisRow - paddingAndBorderAxisRow;
  const float innerHeight = availableHeight - marginAxisColumn - paddingAndBorderAxisColumn;

  if (widthMeasureMode == RYGMeasureModeExactly && heightMeasureMode == RYGMeasureModeExactly) {
    // Don't bother sizing the text if both dimensions are already defined.
    node->layout.measuredDimensions[RYGDimensionWidth] = RYGNodeBoundAxis(
                                                                        node, RYGFlexDirectionRow, availableWidth - marginAxisRow, parentWidth, parentWidth);
    node->layout.measuredDimensions[RYGDimensionHeight] = RYGNodeBoundAxis(
                                                                         node, RYGFlexDirectionColumn, availableHeight - marginAxisColumn, parentHeight, parentWidth);
  } else if (innerWidth <= 0.0f || innerHeight <= 0.0f) {
    // Don't bother sizing the text if there's no horizontal or vertical
    // space.
    node->layout.measuredDimensions[RYGDimensionWidth] =
    RYGNodeBoundAxis(node, RYGFlexDirectionRow, 0.0f, availableWidth, availableWidth);
    node->layout.measuredDimensions[RYGDimensionHeight] =
    RYGNodeBoundAxis(node, RYGFlexDirectionColumn, 0.0f, availableHeight, availableWidth);
  } else {
    // Measure the text under the current constraints.
    const RYGSize measuredSize =
    node->measure(node, innerWidth, widthMeasureMode, innerHeight, heightMeasureMode);

    node->layout.measuredDimensions[RYGDimensionWidth] =
    RYGNodeBoundAxis(node,
                    RYGFlexDirectionRow,
                    (widthMeasureMode == RYGMeasureModeUndefined ||
                     widthMeasureMode == RYGMeasureModeAtMost)
                    ? measuredSize.width + paddingAndBorderAxisRow
                    : availableWidth - marginAxisRow,
                    availableWidth,
                    availableWidth);
    node->layout.measuredDimensions[RYGDimensionHeight] =
    RYGNodeBoundAxis(node,
                    RYGFlexDirectionColumn,
                    (heightMeasureMode == RYGMeasureModeUndefined ||
                     heightMeasureMode == RYGMeasureModeAtMost)
                    ? measuredSize.height + paddingAndBorderAxisColumn
                    : availableHeight - marginAxisColumn,
                    availableHeight,
                    availableWidth);
  }
}

// For nodes with no children, use the available values if they were provided,
// or the minimum size as indicated by the padding and border sizes.
static void RYGNodeEmptyContainerSetMeasuredDimensions(const RYGNodeRef node,
                                                      const float availableWidth,
                                                      const float availableHeight,
                                                      const RYGMeasureMode widthMeasureMode,
                                                      const RYGMeasureMode heightMeasureMode,
                                                      const float parentWidth,
                                                      const float parentHeight) {
  const float paddingAndBorderAxisRow =
  RYGNodePaddingAndBorderForAxis(node, RYGFlexDirectionRow, parentWidth);
  const float paddingAndBorderAxisColumn =
  RYGNodePaddingAndBorderForAxis(node, RYGFlexDirectionColumn, parentWidth);
  const float marginAxisRow = RYGNodeMarginForAxis(node, RYGFlexDirectionRow, parentWidth);
  const float marginAxisColumn = RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, parentWidth);

  node->layout.measuredDimensions[RYGDimensionWidth] =
  RYGNodeBoundAxis(node,
                  RYGFlexDirectionRow,
                  (widthMeasureMode == RYGMeasureModeUndefined ||
                   widthMeasureMode == RYGMeasureModeAtMost)
                  ? paddingAndBorderAxisRow
                  : availableWidth - marginAxisRow,
                  parentWidth,
                  parentWidth);
  node->layout.measuredDimensions[RYGDimensionHeight] =
  RYGNodeBoundAxis(node,
                  RYGFlexDirectionColumn,
                  (heightMeasureMode == RYGMeasureModeUndefined ||
                   heightMeasureMode == RYGMeasureModeAtMost)
                  ? paddingAndBorderAxisColumn
                  : availableHeight - marginAxisColumn,
                  parentHeight,
                  parentWidth);
}

static bool RYGNodeFixedSizeSetMeasuredDimensions(const RYGNodeRef node,
                                                 const float availableWidth,
                                                 const float availableHeight,
                                                 const RYGMeasureMode widthMeasureMode,
                                                 const RYGMeasureMode heightMeasureMode,
                                                 const float parentWidth,
                                                 const float parentHeight) {
  if ((widthMeasureMode == RYGMeasureModeAtMost && availableWidth <= 0.0f) ||
      (heightMeasureMode == RYGMeasureModeAtMost && availableHeight <= 0.0f) ||
      (widthMeasureMode == RYGMeasureModeExactly && heightMeasureMode == RYGMeasureModeExactly)) {
    const float marginAxisColumn = RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, parentWidth);
    const float marginAxisRow = RYGNodeMarginForAxis(node, RYGFlexDirectionRow, parentWidth);

    node->layout.measuredDimensions[RYGDimensionWidth] =
    RYGNodeBoundAxis(node,
                    RYGFlexDirectionRow,
                    RYGFloatIsUndefined(availableWidth) ||
                    (widthMeasureMode == RYGMeasureModeAtMost && availableWidth < 0.0f)
                    ? 0.0f
                    : availableWidth - marginAxisRow,
                    parentWidth,
                    parentWidth);

    node->layout.measuredDimensions[RYGDimensionHeight] =
    RYGNodeBoundAxis(node,
                    RYGFlexDirectionColumn,
                    RYGFloatIsUndefined(availableHeight) ||
                    (heightMeasureMode == RYGMeasureModeAtMost && availableHeight < 0.0f)
                    ? 0.0f
                    : availableHeight - marginAxisColumn,
                    parentHeight,
                    parentWidth);

    return true;
  }

  return false;
}

static void RYGZeroOutLayoutRecursivly(const RYGNodeRef node) {
  node->layout.dimensions[RYGDimensionHeight] = 0;
  node->layout.dimensions[RYGDimensionWidth] = 0;
  node->layout.position[RYGEdgeTop] = 0;
  node->layout.position[RYGEdgeBottom] = 0;
  node->layout.position[RYGEdgeLeft] = 0;
  node->layout.position[RYGEdgeRight] = 0;
  const uint32_t childCount = RYGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; i++) {
    const RYGNodeRef child = RYGNodeListGet(node->children, i);
    RYGZeroOutLayoutRecursivly(child);
  }
}

//
// This is the main routine that implements a subset of the flexbox layout
// algorithm
// described in the W3C RYG documentation: https://www.w3.org/TR/RYG3-flexbox/.
//
// Limitations of this algorithm, compared to the full standard:
//  * Display property is always assumed to be 'flex' except for Text nodes,
//  which
//    are assumed to be 'inline-flex'.
//  * The 'zIndex' property (or any form of z ordering) is not supported. Nodes
//  are
//    stacked in document order.
//  * The 'order' property is not supported. The order of flex items is always
//  defined
//    by document order.
//  * The 'visibility' property is always assumed to be 'visible'. Values of
//  'collapse'
//    and 'hidden' are not supported.
//  * There is no support for forced breaks.
//  * It does not support vertical inline directions (top-to-bottom or
//  bottom-to-top text).
//
// Deviations from standard:
//  * Section 4.5 of the spec indicates that all flex items have a default
//  minimum
//    main size. For text blocks, for example, this is the width of the widest
//    word.
//    Calculating the minimum width is expensive, so we forego it and assume a
//    default
//    minimum main size of 0.
//  * Min/Max sizes in the main axis are not honored when resolving flexible
//  lengths.
//  * The spec indicates that the default value for 'flexDirection' is 'row',
//  but
//    the algorithm below assumes a default of 'column'.
//
// Input parameters:
//    - node: current node to be sized and layed out
//    - availableWidth & availableHeight: available size to be used for sizing
//    the node
//      or RYGUndefined if the size is not available; interpretation depends on
//      layout
//      flags
//    - parentDirection: the inline (text) direction within the parent
//    (left-to-right or
//      right-to-left)
//    - widthMeasureMode: indicates the sizing rules for the width (see below
//    for explanation)
//    - heightMeasureMode: indicates the sizing rules for the height (see below
//    for explanation)
//    - performLayout: specifies whether the caller is interested in just the
//    dimensions
//      of the node or it requires the entire node and its subtree to be layed
//      out
//      (with final positions)
//
// Details:
//    This routine is called recursively to lay out subtrees of flexbox
//    elements. It uses the
//    information in node.style, which is treated as a read-only input. It is
//    responsible for
//    setting the layout.direction and layout.measuredDimensions fields for the
//    input node as well
//    as the layout.position and layout.lineIndex fields for its child nodes.
//    The
//    layout.measuredDimensions field includes any border or padding for the
//    node but does
//    not include margins.
//
//    The spec describes four different layout modes: "fill available", "max
//    content", "min
//    content",
//    and "fit content". Of these, we don't use "min content" because we don't
//    support default
//    minimum main sizes (see above for details). Each of our measure modes maps
//    to a layout mode
//    from the spec (https://www.w3.org/TR/RYG3-sizing/#terms):
//      - RYGMeasureModeUndefined: max content
//      - RYGMeasureModeExactly: fill available
//      - RYGMeasureModeAtMost: fit content
//
//    When calling RYGNodelayoutImpl and RYGLayoutNodeInternal, if the caller passes
//    an available size of
//    undefined then it must also pass a measure mode of RYGMeasureModeUndefined
//    in that dimension.
//
static void RYGNodelayoutImpl(const RYGNodeRef node,
                             const float availableWidth,
                             const float availableHeight,
                             const RYGDirection parentDirection,
                             const RYGMeasureMode widthMeasureMode,
                             const RYGMeasureMode heightMeasureMode,
                             const float parentWidth,
                             const float parentHeight,
                             const bool performLayout) {
  RYG_ASSERT(RYGFloatIsUndefined(availableWidth) ? widthMeasureMode == RYGMeasureModeUndefined : true,
            "availableWidth is indefinite so widthMeasureMode must be "
            "RYGMeasureModeUndefined");
  RYG_ASSERT(RYGFloatIsUndefined(availableHeight) ? heightMeasureMode == RYGMeasureModeUndefined
            : true,
            "availableHeight is indefinite so heightMeasureMode must be "
            "RYGMeasureModeUndefined");

  // Set the resolved resolution in the node's layout.
  const RYGDirection direction = RYGNodeResolveDirection(node, parentDirection);
  node->layout.direction = direction;

  const RYGFlexDirection flexRowDirection = RYGFlexDirectionResolve(RYGFlexDirectionRow, direction);
  const RYGFlexDirection flexColumnDirection =
  RYGFlexDirectionResolve(RYGFlexDirectionColumn, direction);

  node->layout.margin[RYGEdgeStart] = RYGNodeLeadingMargin(node, flexRowDirection, parentWidth);
  node->layout.margin[RYGEdgeEnd] = RYGNodeTrailingMargin(node, flexRowDirection, parentWidth);
  node->layout.margin[RYGEdgeTop] = RYGNodeLeadingMargin(node, flexColumnDirection, parentWidth);
  node->layout.margin[RYGEdgeBottom] = RYGNodeTrailingMargin(node, flexColumnDirection, parentWidth);

  node->layout.border[RYGEdgeStart] = RYGNodeLeadingBorder(node, flexRowDirection);
  node->layout.border[RYGEdgeEnd] = RYGNodeTrailingBorder(node, flexRowDirection);
  node->layout.border[RYGEdgeTop] = RYGNodeLeadingBorder(node, flexColumnDirection);
  node->layout.border[RYGEdgeBottom] = RYGNodeTrailingBorder(node, flexColumnDirection);

  node->layout.padding[RYGEdgeStart] = RYGNodeLeadingPadding(node, flexRowDirection, parentWidth);
  node->layout.padding[RYGEdgeEnd] = RYGNodeTrailingPadding(node, flexRowDirection, parentWidth);
  node->layout.padding[RYGEdgeTop] = RYGNodeLeadingPadding(node, flexColumnDirection, parentWidth);
  node->layout.padding[RYGEdgeBottom] =
  RYGNodeTrailingPadding(node, flexColumnDirection, parentWidth);

  if (node->measure) {
    RYGNodeWithMeasureFuncSetMeasuredDimensions(node,
                                               availableWidth,
                                               availableHeight,
                                               widthMeasureMode,
                                               heightMeasureMode,
                                               parentWidth,
                                               parentHeight);
    return;
  }

  const uint32_t childCount = RYGNodeListCount(node->children);
  if (childCount == 0) {
    RYGNodeEmptyContainerSetMeasuredDimensions(node,
                                              availableWidth,
                                              availableHeight,
                                              widthMeasureMode,
                                              heightMeasureMode,
                                              parentWidth,
                                              parentHeight);
    return;
  }

  // If we're not being asked to perform a full layout we can skip the algorithm if we already know
  // the size
  if (!performLayout && RYGNodeFixedSizeSetMeasuredDimensions(node,
                                                             availableWidth,
                                                             availableHeight,
                                                             widthMeasureMode,
                                                             heightMeasureMode,
                                                             parentWidth,
                                                             parentHeight)) {
    return;
  }

  // STEP 1: CALCULATE VALUES FOR REMAINDER OF ALGORITHM
  const RYGFlexDirection mainAxis = RYGFlexDirectionResolve(node->style.flexDirection, direction);
  const RYGFlexDirection crossAxis = RYGFlexDirectionCross(mainAxis, direction);
  const bool isMainAxisRow = RYGFlexDirectionIsRow(mainAxis);
  const RYGJustify justifyContent = node->style.justifyContent;
  const bool isNodeFlexWrap = node->style.flexWrap != RYGWrapNoWrap;

  const float mainAxisParentSize = isMainAxisRow ? parentWidth : parentHeight;
  const float crossAxisParentSize = isMainAxisRow ? parentHeight : parentWidth;

  RYGNodeRef firstAbsoluteChild = NULL;
  RYGNodeRef currentAbsoluteChild = NULL;

  const float leadingPaddingAndBorderMain =
  RYGNodeLeadingPaddingAndBorder(node, mainAxis, parentWidth);
  const float trailingPaddingAndBorderMain =
  RYGNodeTrailingPaddingAndBorder(node, mainAxis, parentWidth);
  const float leadingPaddingAndBorderCross =
  RYGNodeLeadingPaddingAndBorder(node, crossAxis, parentWidth);
  const float paddingAndBorderAxisMain = RYGNodePaddingAndBorderForAxis(node, mainAxis, parentWidth);
  const float paddingAndBorderAxisCross =
  RYGNodePaddingAndBorderForAxis(node, crossAxis, parentWidth);

  const RYGMeasureMode measureModeMainDim = isMainAxisRow ? widthMeasureMode : heightMeasureMode;
  const RYGMeasureMode measureModeCrossDim = isMainAxisRow ? heightMeasureMode : widthMeasureMode;

  const float paddingAndBorderAxisRow =
  isMainAxisRow ? paddingAndBorderAxisMain : paddingAndBorderAxisCross;
  const float paddingAndBorderAxisColumn =
  isMainAxisRow ? paddingAndBorderAxisCross : paddingAndBorderAxisMain;

  const float marginAxisRow = RYGNodeMarginForAxis(node, RYGFlexDirectionRow, parentWidth);
  const float marginAxisColumn = RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, parentWidth);

  // STEP 2: DETERMINE AVAILABLE SIZE IN MAIN AND CROSS DIRECTIONS
  const float minInnerWidth =
  RYGValueResolve(&node->style.minDimensions[RYGDimensionWidth], parentWidth) - marginAxisRow -
  paddingAndBorderAxisRow;
  const float maxInnerWidth =
  RYGValueResolve(&node->style.maxDimensions[RYGDimensionWidth], parentWidth) - marginAxisRow -
  paddingAndBorderAxisRow;
  const float minInnerHeight =
  RYGValueResolve(&node->style.minDimensions[RYGDimensionHeight], parentHeight) -
  marginAxisColumn - paddingAndBorderAxisColumn;
  const float maxInnerHeight =
  RYGValueResolve(&node->style.maxDimensions[RYGDimensionHeight], parentHeight) -
  marginAxisColumn - paddingAndBorderAxisColumn;
  const float minInnerMainDim = isMainAxisRow ? minInnerWidth : minInnerHeight;
  const float maxInnerMainDim = isMainAxisRow ? maxInnerWidth : maxInnerHeight;

  // Max dimension overrides predefined dimension value; Min dimension in turn overrides both of the
  // above
  float availableInnerWidth = availableWidth - marginAxisRow - paddingAndBorderAxisRow;
  if (!RYGFloatIsUndefined(availableInnerWidth)) {
    availableInnerWidth = fmaxf(fminf(availableInnerWidth, maxInnerWidth), minInnerWidth);
  }

  float availableInnerHeight = availableHeight - marginAxisColumn - paddingAndBorderAxisColumn;
  if (!RYGFloatIsUndefined(availableInnerHeight)) {
    availableInnerHeight = fmaxf(fminf(availableInnerHeight, maxInnerHeight), minInnerHeight);
  }

  float availableInnerMainDim = isMainAxisRow ? availableInnerWidth : availableInnerHeight;
  const float availableInnerCrossDim = isMainAxisRow ? availableInnerHeight : availableInnerWidth;

  // If there is only one child with flexGrow + flexShrink it means we can set the
  // computedFlexBasis to 0 instead of measuring and shrinking / flexing the child to exactly
  // match the remaining space
  RYGNodeRef singleFlexChild = NULL;
  if ((isMainAxisRow && widthMeasureMode == RYGMeasureModeExactly) ||
      (!isMainAxisRow && heightMeasureMode == RYGMeasureModeExactly)) {
    for (uint32_t i = 0; i < childCount; i++) {
      const RYGNodeRef child = RYGNodeGetChild(node, i);
      if (singleFlexChild) {
        if (RYGNodeIsFlex(child)) {
          // There is already a flexible child, abort.
          singleFlexChild = NULL;
          break;
        }
      } else if (RYGNodeStyleGetFlexGrow(child) > 0.0f && RYGNodeStyleGetFlexShrink(child) > 0.0f) {
        singleFlexChild = child;
      }
    }
  }

  float totalFlexBasis = 0;

  // STEP 3: DETERMINE FLEX BASIS FOR EACH ITEM
  for (uint32_t i = 0; i < childCount; i++) {
    const RYGNodeRef child = RYGNodeListGet(node->children, i);
    if (child->style.display == RYGDisplayNone) {
      RYGZeroOutLayoutRecursivly(child);
      child->hasNewLayout = true;
      child->isDirty = false;
      continue;
    }
    RYGResolveDimensions(child);
    if (performLayout) {
      // Set the initial position (relative to the parent).
      const RYGDirection childDirection = RYGNodeResolveDirection(child, direction);
      RYGNodeSetPosition(child,
                        childDirection,
                        availableInnerMainDim,
                        availableInnerCrossDim,
                        availableInnerWidth);
    }

    // Absolute-positioned children don't participate in flex layout. Add them
    // to a list that we can process later.
    if (child->style.positionType == RYGPositionTypeAbsolute) {
      // Store a private linked list of absolutely positioned children
      // so that we can efficiently traverse them later.
      if (firstAbsoluteChild == NULL) {
        firstAbsoluteChild = child;
      }
      if (currentAbsoluteChild != NULL) {
        currentAbsoluteChild->nextChild = child;
      }
      currentAbsoluteChild = child;
      child->nextChild = NULL;
    } else {
      if (child == singleFlexChild) {
        child->layout.computedFlexBasisGeneration = gCurrentGenerationCount;
        child->layout.computedFlexBasis = 0;
      } else {
        RYGNodeComputeFlexBasisForChild(node,
                                       child,
                                       availableInnerWidth,
                                       widthMeasureMode,
                                       availableInnerHeight,
                                       availableInnerWidth,
                                       availableInnerHeight,
                                       heightMeasureMode,
                                       direction);
      }
    }

    totalFlexBasis += child->layout.computedFlexBasis;
  }

  const bool flexBasisOverflows =
  measureModeMainDim == RYGMeasureModeUndefined ? false : totalFlexBasis > availableInnerMainDim;

  // STEP 4: COLLECT FLEX ITEMS INTO FLEX LINES

  // Indexes of children that represent the first and last items in the line.
  uint32_t startOfLineIndex = 0;
  uint32_t endOfLineIndex = 0;

  // Number of lines.
  uint32_t lineCount = 0;

  // Accumulated cross dimensions of all lines so far.
  float totalLineCrossDim = 0;

  // Max main dimension of all the lines.
  float maxLineMainDim = 0;

  for (; endOfLineIndex < childCount; lineCount++, startOfLineIndex = endOfLineIndex) {
    // Number of items on the currently line. May be different than the
    // difference
    // between start and end indicates because we skip over absolute-positioned
    // items.
    uint32_t itemsOnLine = 0;

    // sizeConsumedOnCurrentLine is accumulation of the dimensions and margin
    // of all the children on the current line. This will be used in order to
    // either set the dimensions of the node if none already exist or to compute
    // the remaining space left for the flexible children.
    float sizeConsumedOnCurrentLine = 0;

    float totalFlexGrowFactors = 0;
    float totalFlexShrinkScaledFactors = 0;

    // Maintain a linked list of the child nodes that can shrink and/or grow.
    RYGNodeRef firstRelativeChild = NULL;
    RYGNodeRef currentRelativeChild = NULL;

    // Add items to the current line until it's full or we run out of items.
    for (uint32_t i = startOfLineIndex; i < childCount; i++, endOfLineIndex++) {
      const RYGNodeRef child = RYGNodeListGet(node->children, i);
      if (child->style.display == RYGDisplayNone) {
        continue;
      }
      child->lineIndex = lineCount;

      if (child->style.positionType != RYGPositionTypeAbsolute) {
        const float outerFlexBasis =
        fmaxf(RYGValueResolve(&child->style.minDimensions[dim[mainAxis]], mainAxisParentSize),
              child->layout.computedFlexBasis) +
        RYGNodeMarginForAxis(child, mainAxis, availableInnerWidth);

        // If this is a multi-line flow and this item pushes us over the
        // available size, we've
        // hit the end of the current line. Break out of the loop and lay out
        // the current line.
        if (sizeConsumedOnCurrentLine + outerFlexBasis > availableInnerMainDim && isNodeFlexWrap &&
            itemsOnLine > 0) {
          break;
        }

        sizeConsumedOnCurrentLine += outerFlexBasis;
        itemsOnLine++;

        if (RYGNodeIsFlex(child)) {
          totalFlexGrowFactors += RYGNodeStyleGetFlexGrow(child);

          // Unlike the grow factor, the shrink factor is scaled relative to the
          // child
          // dimension.
          totalFlexShrinkScaledFactors +=
          -RYGNodeStyleGetFlexShrink(child) * child->layout.computedFlexBasis;
        }

        // Store a private linked list of children that need to be layed out.
        if (firstRelativeChild == NULL) {
          firstRelativeChild = child;
        }
        if (currentRelativeChild != NULL) {
          currentRelativeChild->nextChild = child;
        }
        currentRelativeChild = child;
        child->nextChild = NULL;
      }
    }

    // If we don't need to measure the cross axis, we can skip the entire flex
    // step.
    const bool canSkipFlex = !performLayout && measureModeCrossDim == RYGMeasureModeExactly;

    // In order to position the elements in the main axis, we have two
    // controls. The space between the beginning and the first element
    // and the space between each two elements.
    float leadingMainDim = 0;
    float betweenMainDim = 0;

    // STEP 5: RESOLVING FLEXIBLE LENGTHS ON MAIN AXIS
    // Calculate the remaining available space that needs to be allocated.
    // If the main dimension size isn't known, it is computed based on
    // the line length, so there's no more space left to distribute.

    // We resolve main dimension to fit minimum and maximum values
    if (RYGFloatIsUndefined(availableInnerMainDim)) {
      if (!RYGFloatIsUndefined(minInnerMainDim) && sizeConsumedOnCurrentLine < minInnerMainDim) {
        availableInnerMainDim = minInnerMainDim;
      } else if (!RYGFloatIsUndefined(maxInnerMainDim) &&
                 sizeConsumedOnCurrentLine > maxInnerMainDim) {
        availableInnerMainDim = maxInnerMainDim;
      }
    }

    float remainingFreeSpace = 0;
    if (!RYGFloatIsUndefined(availableInnerMainDim)) {
      remainingFreeSpace = availableInnerMainDim - sizeConsumedOnCurrentLine;
    } else if (sizeConsumedOnCurrentLine < 0) {
      // availableInnerMainDim is indefinite which means the node is being sized
      // based on its
      // content.
      // sizeConsumedOnCurrentLine is negative which means the node will
      // allocate 0 points for
      // its content. Consequently, remainingFreeSpace is 0 -
      // sizeConsumedOnCurrentLine.
      remainingFreeSpace = -sizeConsumedOnCurrentLine;
    }

    const float originalRemainingFreeSpace = remainingFreeSpace;
    float deltaFreeSpace = 0;

    if (!canSkipFlex) {
      float childFlexBasis;
      float flexShrinkScaledFactor;
      float flexGrowFactor;
      float baseMainSize;
      float boundMainSize;

      // Do two passes over the flex items to figure out how to distribute the
      // remaining space.
      // The first pass finds the items whose min/max constraints trigger,
      // freezes them at those
      // sizes, and excludes those sizes from the remaining space. The second
      // pass sets the size
      // of each flexible item. It distributes the remaining space amongst the
      // items whose min/max
      // constraints didn't trigger in pass 1. For the other items, it sets
      // their sizes by forcing
      // their min/max constraints to trigger again.
      //
      // This two pass approach for resolving min/max constraints deviates from
      // the spec. The
      // spec (https://www.w3.org/TR/RYG-flexbox-1/#resolve-flexible-lengths)
      // describes a process
      // that needs to be repeated a variable number of times. The algorithm
      // implemented here
      // won't handle all cases but it was simpler to implement and it mitigates
      // performance
      // concerns because we know exactly how many passes it'll do.

      // First pass: detect the flex items whose min/max constraints trigger
      float deltaFlexShrinkScaledFactors = 0;
      float deltaFlexGrowFactors = 0;
      currentRelativeChild = firstRelativeChild;
      while (currentRelativeChild != NULL) {
        childFlexBasis = currentRelativeChild->layout.computedFlexBasis;

        if (remainingFreeSpace < 0) {
          flexShrinkScaledFactor = -RYGNodeStyleGetFlexShrink(currentRelativeChild) * childFlexBasis;

          // Is this child able to shrink?
          if (flexShrinkScaledFactor != 0) {
            baseMainSize =
            childFlexBasis +
            remainingFreeSpace / totalFlexShrinkScaledFactors * flexShrinkScaledFactor;
            boundMainSize = RYGNodeBoundAxis(currentRelativeChild,
                                            mainAxis,
                                            baseMainSize,
                                            availableInnerMainDim,
                                            availableInnerWidth);
            if (baseMainSize != boundMainSize) {
              // By excluding this item's size and flex factor from remaining,
              // this item's
              // min/max constraints should also trigger in the second pass
              // resulting in the
              // item's size calculation being identical in the first and second
              // passes.
              deltaFreeSpace -= boundMainSize - childFlexBasis;
              deltaFlexShrinkScaledFactors -= flexShrinkScaledFactor;
            }
          }
        } else if (remainingFreeSpace > 0) {
          flexGrowFactor = RYGNodeStyleGetFlexGrow(currentRelativeChild);

          // Is this child able to grow?
          if (flexGrowFactor != 0) {
            baseMainSize =
            childFlexBasis + remainingFreeSpace / totalFlexGrowFactors * flexGrowFactor;
            boundMainSize = RYGNodeBoundAxis(currentRelativeChild,
                                            mainAxis,
                                            baseMainSize,
                                            availableInnerMainDim,
                                            availableInnerWidth);
            if (baseMainSize != boundMainSize) {
              // By excluding this item's size and flex factor from remaining,
              // this item's
              // min/max constraints should also trigger in the second pass
              // resulting in the
              // item's size calculation being identical in the first and second
              // passes.
              deltaFreeSpace -= boundMainSize - childFlexBasis;
              deltaFlexGrowFactors -= flexGrowFactor;
            }
          }
        }

        currentRelativeChild = currentRelativeChild->nextChild;
      }

      totalFlexShrinkScaledFactors += deltaFlexShrinkScaledFactors;
      totalFlexGrowFactors += deltaFlexGrowFactors;
      remainingFreeSpace += deltaFreeSpace;

      // Second pass: resolve the sizes of the flexible items
      deltaFreeSpace = 0;
      currentRelativeChild = firstRelativeChild;
      while (currentRelativeChild != NULL) {
        childFlexBasis = currentRelativeChild->layout.computedFlexBasis;
        float updatedMainSize = childFlexBasis;

        if (remainingFreeSpace < 0) {
          flexShrinkScaledFactor = -RYGNodeStyleGetFlexShrink(currentRelativeChild) * childFlexBasis;
          // Is this child able to shrink?
          if (flexShrinkScaledFactor != 0) {
            float childSize;

            if (totalFlexShrinkScaledFactors == 0) {
              childSize = childFlexBasis + flexShrinkScaledFactor;
            } else {
              childSize =
              childFlexBasis +
              (remainingFreeSpace / totalFlexShrinkScaledFactors) * flexShrinkScaledFactor;
            }

            updatedMainSize = RYGNodeBoundAxis(currentRelativeChild,
                                              mainAxis,
                                              childSize,
                                              availableInnerMainDim,
                                              availableInnerWidth);
          }
        } else if (remainingFreeSpace > 0) {
          flexGrowFactor = RYGNodeStyleGetFlexGrow(currentRelativeChild);

          // Is this child able to grow?
          if (flexGrowFactor != 0) {
            updatedMainSize =
            RYGNodeBoundAxis(currentRelativeChild,
                            mainAxis,
                            childFlexBasis +
                            remainingFreeSpace / totalFlexGrowFactors * flexGrowFactor,
                            availableInnerMainDim,
                            availableInnerWidth);
          }
        }

        deltaFreeSpace -= updatedMainSize - childFlexBasis;

        const float marginMain =
        RYGNodeMarginForAxis(currentRelativeChild, mainAxis, availableInnerWidth);
        const float marginCross =
        RYGNodeMarginForAxis(currentRelativeChild, crossAxis, availableInnerWidth);

        float childCrossSize;
        float childMainSize = updatedMainSize + marginMain;
        RYGMeasureMode childCrossMeasureMode;
        RYGMeasureMode childMainMeasureMode = RYGMeasureModeExactly;

        if (!RYGFloatIsUndefined(availableInnerCrossDim) &&
            !RYGNodeIsStyleDimDefined(currentRelativeChild, crossAxis, availableInnerCrossDim) &&
            measureModeCrossDim == RYGMeasureModeExactly &&
            !(isNodeFlexWrap && flexBasisOverflows) &&
            RYGNodeAlignItem(node, currentRelativeChild) == RYGAlignStretch) {
          childCrossSize = availableInnerCrossDim;
          childCrossMeasureMode = RYGMeasureModeExactly;
        } else if (!RYGNodeIsStyleDimDefined(currentRelativeChild,
                                            crossAxis,
                                            availableInnerCrossDim)) {
          childCrossSize = availableInnerCrossDim;
          childCrossMeasureMode =
          RYGFloatIsUndefined(childCrossSize) ? RYGMeasureModeUndefined : RYGMeasureModeAtMost;
        } else {
          childCrossSize = RYGValueResolve(currentRelativeChild->resolvedDimensions[dim[crossAxis]],
                                          availableInnerCrossDim) +
          marginCross;
          childCrossMeasureMode =
          RYGFloatIsUndefined(childCrossSize) ? RYGMeasureModeUndefined : RYGMeasureModeExactly;
        }

        if (!RYGFloatIsUndefined(currentRelativeChild->style.aspectRatio)) {
          childCrossSize = fmaxf(
                                 isMainAxisRow
                                 ? (childMainSize - marginMain) / currentRelativeChild->style.aspectRatio
                                 : (childMainSize - marginMain) * currentRelativeChild->style.aspectRatio,
                                 RYGNodePaddingAndBorderForAxis(currentRelativeChild, crossAxis, availableInnerWidth));
          childCrossMeasureMode = RYGMeasureModeExactly;

          // Parent size constraint should have higher priority than flex
          if (RYGNodeIsFlex(currentRelativeChild)) {
            childCrossSize = fminf(childCrossSize - marginCross, availableInnerCrossDim);
            childMainSize =
            marginMain + (isMainAxisRow
                          ? childCrossSize * currentRelativeChild->style.aspectRatio
                          : childCrossSize / currentRelativeChild->style.aspectRatio);
          }

          childCrossSize += marginCross;
        }

        RYGConstrainMaxSizeForMode(
                                  RYGValueResolve(&currentRelativeChild->style.maxDimensions[dim[mainAxis]],
                                                 availableInnerWidth),
                                  &childMainMeasureMode,
                                  &childMainSize);
        RYGConstrainMaxSizeForMode(
                                  RYGValueResolve(&currentRelativeChild->style.maxDimensions[dim[crossAxis]],
                                                 availableInnerHeight),
                                  &childCrossMeasureMode,
                                  &childCrossSize);

        const bool requiresStretchLayout =
        !RYGNodeIsStyleDimDefined(currentRelativeChild, crossAxis, availableInnerCrossDim) &&
        RYGNodeAlignItem(node, currentRelativeChild) == RYGAlignStretch;

        const float childWidth = isMainAxisRow ? childMainSize : childCrossSize;
        const float childHeight = !isMainAxisRow ? childMainSize : childCrossSize;

        const RYGMeasureMode childWidthMeasureMode =
        isMainAxisRow ? childMainMeasureMode : childCrossMeasureMode;
        const RYGMeasureMode childHeightMeasureMode =
        !isMainAxisRow ? childMainMeasureMode : childCrossMeasureMode;

        // Recursively call the layout algorithm for this child with the updated
        // main size.
        RYGLayoutNodeInternal(currentRelativeChild,
                             childWidth,
                             childHeight,
                             direction,
                             childWidthMeasureMode,
                             childHeightMeasureMode,
                             availableInnerWidth,
                             availableInnerHeight,
                             performLayout && !requiresStretchLayout,
                             "flex");

        currentRelativeChild = currentRelativeChild->nextChild;
      }
    }

    remainingFreeSpace = originalRemainingFreeSpace + deltaFreeSpace;

    // STEP 6: MAIN-AXIS JUSTIFICATION & CROSS-AXIS SIZE DETERMINATION

    // At this point, all the children have their dimensions set in the main
    // axis.
    // Their dimensions are also set in the cross axis with the exception of
    // items
    // that are aligned "stretch". We need to compute these stretch values and
    // set the final positions.

    // If we are using "at most" rules in the main axis. Calculate the remaining space when
    // constraint by the min size defined for the main axis.

    if (measureModeMainDim == RYGMeasureModeAtMost && remainingFreeSpace > 0) {
      if (node->style.minDimensions[dim[mainAxis]].unit != RYGUnitUndefined &&
          RYGValueResolve(&node->style.minDimensions[dim[mainAxis]], mainAxisParentSize) >= 0) {
        remainingFreeSpace =
        fmaxf(0,
              RYGValueResolve(&node->style.minDimensions[dim[mainAxis]], mainAxisParentSize) -
              (availableInnerMainDim - remainingFreeSpace));
      } else {
        remainingFreeSpace = 0;
      }
    }

    int numberOfAutoMarginsOnCurrentLine = 0;
    for (uint32_t i = startOfLineIndex; i < endOfLineIndex; i++) {
      const RYGNodeRef child = RYGNodeListGet(node->children, i);
      if (child->style.positionType == RYGPositionTypeRelative) {
        if (child->style.margin[leading[mainAxis]].unit == RYGUnitAuto) {
          numberOfAutoMarginsOnCurrentLine++;
        }
        if (child->style.margin[trailing[mainAxis]].unit == RYGUnitAuto) {
          numberOfAutoMarginsOnCurrentLine++;
        }
      }
    }

    if (numberOfAutoMarginsOnCurrentLine == 0) {
      switch (justifyContent) {
        case RYGJustifyCenter:
          leadingMainDim = remainingFreeSpace / 2;
          break;
        case RYGJustifyFlexEnd:
          leadingMainDim = remainingFreeSpace;
          break;
        case RYGJustifySpaceBetween:
          if (itemsOnLine > 1) {
            betweenMainDim = fmaxf(remainingFreeSpace, 0) / (itemsOnLine - 1);
          } else {
            betweenMainDim = 0;
          }
          break;
        case RYGJustifySpaceAround:
          // Space on the edges is half of the space between elements
          betweenMainDim = remainingFreeSpace / itemsOnLine;
          leadingMainDim = betweenMainDim / 2;
          break;
        case RYGJustifyFlexStart:
          break;
      }
    }

    float mainDim = leadingPaddingAndBorderMain + leadingMainDim;
    float crossDim = 0;

    for (uint32_t i = startOfLineIndex; i < endOfLineIndex; i++) {
      const RYGNodeRef child = RYGNodeListGet(node->children, i);
      if (child->style.display == RYGDisplayNone) {
        continue;
      }
      if (child->style.positionType == RYGPositionTypeAbsolute &&
          RYGNodeIsLeadingPosDefined(child, mainAxis)) {
        if (performLayout) {
          // In case the child is position absolute and has left/top being
          // defined, we override the position to whatever the user said
          // (and margin/border).
          child->layout.position[pos[mainAxis]] =
          RYGNodeLeadingPosition(child, mainAxis, availableInnerMainDim) +
          RYGNodeLeadingBorder(node, mainAxis) +
          RYGNodeLeadingMargin(child, mainAxis, availableInnerWidth);
        }
      } else {
        // Now that we placed the element, we need to update the variables.
        // We need to do that only for relative elements. Absolute elements
        // do not take part in that phase.
        if (child->style.positionType == RYGPositionTypeRelative) {
          if (child->style.margin[leading[mainAxis]].unit == RYGUnitAuto) {
            mainDim += remainingFreeSpace / numberOfAutoMarginsOnCurrentLine;
          }

          if (performLayout) {
            child->layout.position[pos[mainAxis]] += mainDim;
          }

          if (child->style.margin[trailing[mainAxis]].unit == RYGUnitAuto) {
            mainDim += remainingFreeSpace / numberOfAutoMarginsOnCurrentLine;
          }

          if (canSkipFlex) {
            // If we skipped the flex step, then we can't rely on the
            // measuredDims because
            // they weren't computed. This means we can't call RYGNodeDimWithMargin.
            mainDim += betweenMainDim + RYGNodeMarginForAxis(child, mainAxis, availableInnerWidth) +
            child->layout.computedFlexBasis;
            crossDim = availableInnerCrossDim;
          } else {
            // The main dimension is the sum of all the elements dimension plus the spacing.
            mainDim += betweenMainDim + RYGNodeDimWithMargin(child, mainAxis, availableInnerWidth);

            // The cross dimension is the max of the elements dimension since
            // there can only be one element in that cross dimension.
            crossDim = fmaxf(crossDim, RYGNodeDimWithMargin(child, crossAxis, availableInnerWidth));
          }
        } else if (performLayout) {
          child->layout.position[pos[mainAxis]] +=
          RYGNodeLeadingBorder(node, mainAxis) + leadingMainDim;
        }
      }
    }

    mainDim += trailingPaddingAndBorderMain;

    float containerCrossAxis = availableInnerCrossDim;
    if (measureModeCrossDim == RYGMeasureModeUndefined ||
        measureModeCrossDim == RYGMeasureModeAtMost) {
      // Compute the cross axis from the max cross dimension of the children.
      containerCrossAxis = RYGNodeBoundAxis(node,
                                           crossAxis,
                                           crossDim + paddingAndBorderAxisCross,
                                           crossAxisParentSize,
                                           parentWidth) -
      paddingAndBorderAxisCross;

      if (measureModeCrossDim == RYGMeasureModeAtMost) {
        containerCrossAxis = fminf(containerCrossAxis, availableInnerCrossDim);
      }
    }

    // If there's no flex wrap, the cross dimension is defined by the container.
    if (!isNodeFlexWrap && measureModeCrossDim == RYGMeasureModeExactly) {
      crossDim = availableInnerCrossDim;
    }

    // Clamp to the min/max size specified on the container.
    crossDim = RYGNodeBoundAxis(node,
                               crossAxis,
                               crossDim + paddingAndBorderAxisCross,
                               crossAxisParentSize,
                               parentWidth) -
    paddingAndBorderAxisCross;

    // STEP 7: CROSS-AXIS ALIGNMENT
    // We can skip child alignment if we're just measuring the container.
    if (performLayout) {
      for (uint32_t i = startOfLineIndex; i < endOfLineIndex; i++) {
        const RYGNodeRef child = RYGNodeListGet(node->children, i);
        if (child->style.display == RYGDisplayNone) {
          continue;
        }
        if (child->style.positionType == RYGPositionTypeAbsolute) {
          // If the child is absolutely positioned and has a
          // top/left/bottom/right
          // set, override all the previously computed positions to set it
          // correctly.
          if (RYGNodeIsLeadingPosDefined(child, crossAxis)) {
            child->layout.position[pos[crossAxis]] =
            RYGNodeLeadingPosition(child, crossAxis, availableInnerCrossDim) +
            RYGNodeLeadingBorder(node, crossAxis) +
            RYGNodeLeadingMargin(child, crossAxis, availableInnerWidth);
          } else {
            child->layout.position[pos[crossAxis]] =
            RYGNodeLeadingBorder(node, crossAxis) +
            RYGNodeLeadingMargin(child, crossAxis, availableInnerWidth);
          }
        } else {
          float leadingCrossDim = leadingPaddingAndBorderCross;

          // For a relative children, we're either using alignItems (parent) or
          // alignSelf (child) in order to determine the position in the cross
          // axis
          const RYGAlign alignItem = RYGNodeAlignItem(node, child);

          // If the child uses align stretch, we need to lay it out one more
          // time, this time
          // forcing the cross-axis size to be the computed cross size for the
          // current line.
          if (alignItem == RYGAlignStretch &&
              child->style.margin[leading[crossAxis]].unit != RYGUnitAuto &&
              child->style.margin[trailing[crossAxis]].unit != RYGUnitAuto) {
            // If the child defines a definite size for its cross axis, there's
            // no need to stretch.
            if (!RYGNodeIsStyleDimDefined(child, crossAxis, availableInnerCrossDim)) {
              float childMainSize = child->layout.measuredDimensions[dim[mainAxis]];
              float childCrossSize =
              !RYGFloatIsUndefined(child->style.aspectRatio)
              ? ((RYGNodeMarginForAxis(child, crossAxis, availableInnerWidth) +
                  (isMainAxisRow ? childMainSize / child->style.aspectRatio
                   : childMainSize * child->style.aspectRatio)))
              : crossDim;

              childMainSize += RYGNodeMarginForAxis(child, mainAxis, availableInnerWidth);

              RYGMeasureMode childMainMeasureMode = RYGMeasureModeExactly;
              RYGMeasureMode childCrossMeasureMode = RYGMeasureModeExactly;
              RYGConstrainMaxSizeForMode(RYGValueResolve(&child->style.maxDimensions[dim[mainAxis]],
                                                       availableInnerMainDim),
                                        &childMainMeasureMode,
                                        &childMainSize);
              RYGConstrainMaxSizeForMode(RYGValueResolve(&child->style.maxDimensions[dim[crossAxis]],
                                                       availableInnerCrossDim),
                                        &childCrossMeasureMode,
                                        &childCrossSize);

              const float childWidth = isMainAxisRow ? childMainSize : childCrossSize;
              const float childHeight = !isMainAxisRow ? childMainSize : childCrossSize;

              const RYGMeasureMode childWidthMeasureMode =
              RYGFloatIsUndefined(childWidth) ? RYGMeasureModeUndefined : RYGMeasureModeExactly;
              const RYGMeasureMode childHeightMeasureMode =
              RYGFloatIsUndefined(childHeight) ? RYGMeasureModeUndefined : RYGMeasureModeExactly;

              RYGLayoutNodeInternal(child,
                                   childWidth,
                                   childHeight,
                                   direction,
                                   childWidthMeasureMode,
                                   childHeightMeasureMode,
                                   availableInnerWidth,
                                   availableInnerHeight,
                                   true,
                                   "stretch");
            }
          } else {
            const float remainingCrossDim =
            containerCrossAxis - RYGNodeDimWithMargin(child, crossAxis, availableInnerWidth);

            if (child->style.margin[leading[crossAxis]].unit == RYGUnitAuto &&
                child->style.margin[trailing[crossAxis]].unit == RYGUnitAuto) {
              leadingCrossDim += remainingCrossDim / 2;
            } else if (child->style.margin[trailing[crossAxis]].unit == RYGUnitAuto) {
              // No-Op
            } else if (child->style.margin[leading[crossAxis]].unit == RYGUnitAuto) {
              leadingCrossDim += remainingCrossDim;
            } else if (alignItem == RYGAlignFlexStart) {
              // No-Op
            } else if (alignItem == RYGAlignCenter) {
              leadingCrossDim += remainingCrossDim / 2;
            } else {
              leadingCrossDim += remainingCrossDim;
            }
          }
          // And we apply the position
          child->layout.position[pos[crossAxis]] += totalLineCrossDim + leadingCrossDim;
        }
      }
    }

    totalLineCrossDim += crossDim;
    maxLineMainDim = fmaxf(maxLineMainDim, mainDim);
  }

  // STEP 8: MULTI-LINE CONTENT ALIGNMENT
  if (performLayout &&
      (lineCount > 1 || node->style.alignContent == RYGAlignStretch || RYGIsBaselineLayout(node)) &&
      !RYGFloatIsUndefined(availableInnerCrossDim)) {
    const float remainingAlignContentDim = availableInnerCrossDim - totalLineCrossDim;

    float crossDimLead = 0;
    float currentLead = leadingPaddingAndBorderCross;

    switch (node->style.alignContent) {
      case RYGAlignFlexEnd:
        currentLead += remainingAlignContentDim;
        break;
      case RYGAlignCenter:
        currentLead += remainingAlignContentDim / 2;
        break;
      case RYGAlignStretch:
        if (availableInnerCrossDim > totalLineCrossDim) {
          crossDimLead = remainingAlignContentDim / lineCount;
        }
        break;
      case RYGAlignSpaceAround:
        if (availableInnerCrossDim > totalLineCrossDim) {
          currentLead += remainingAlignContentDim / (2 * lineCount);
          if (lineCount > 1) {
            crossDimLead = remainingAlignContentDim / lineCount;
          }
        } else {
          currentLead += remainingAlignContentDim / 2;
        }
        break;
      case RYGAlignSpaceBetween:
        if (availableInnerCrossDim > totalLineCrossDim && lineCount > 1) {
          crossDimLead = remainingAlignContentDim / (lineCount - 1);
        }
        break;
      case RYGAlignAuto:
      case RYGAlignFlexStart:
      case RYGAlignBaseline:
        break;
    }

    uint32_t endIndex = 0;
    for (uint32_t i = 0; i < lineCount; i++) {
      const uint32_t startIndex = endIndex;
      uint32_t ii;

      // compute the line's height and find the endIndex
      float lineHeight = 0;
      float maxAscentForCurrentLine = 0;
      float maxDescentForCurrentLine = 0;
      for (ii = startIndex; ii < childCount; ii++) {
        const RYGNodeRef child = RYGNodeListGet(node->children, ii);
        if (child->style.display == RYGDisplayNone) {
          continue;
        }
        if (child->style.positionType == RYGPositionTypeRelative) {
          if (child->lineIndex != i) {
            break;
          }
          if (RYGNodeIsLayoutDimDefined(child, crossAxis)) {
            lineHeight = fmaxf(lineHeight,
                               child->layout.measuredDimensions[dim[crossAxis]] +
                               RYGNodeMarginForAxis(child, crossAxis, availableInnerWidth));
          }
          if (RYGNodeAlignItem(node, child) == RYGAlignBaseline) {
            const float ascent =
            RYGBaseline(child) +
            RYGNodeLeadingMargin(child, RYGFlexDirectionColumn, availableInnerWidth);
            const float descent =
            child->layout.measuredDimensions[RYGDimensionHeight] +
            RYGNodeMarginForAxis(child, RYGFlexDirectionColumn, availableInnerWidth) - ascent;
            maxAscentForCurrentLine = fmaxf(maxAscentForCurrentLine, ascent);
            maxDescentForCurrentLine = fmaxf(maxDescentForCurrentLine, descent);
            lineHeight = fmaxf(lineHeight, maxAscentForCurrentLine + maxDescentForCurrentLine);
          }
        }
      }
      endIndex = ii;
      lineHeight += crossDimLead;

      if (performLayout) {
        for (ii = startIndex; ii < endIndex; ii++) {
          const RYGNodeRef child = RYGNodeListGet(node->children, ii);
          if (child->style.display == RYGDisplayNone) {
            continue;
          }
          if (child->style.positionType == RYGPositionTypeRelative) {
            switch (RYGNodeAlignItem(node, child)) {
              case RYGAlignFlexStart: {
                child->layout.position[pos[crossAxis]] =
                currentLead + RYGNodeLeadingMargin(child, crossAxis, availableInnerWidth);
                break;
              }
              case RYGAlignFlexEnd: {
                child->layout.position[pos[crossAxis]] =
                currentLead + lineHeight -
                RYGNodeTrailingMargin(child, crossAxis, availableInnerWidth) -
                child->layout.measuredDimensions[dim[crossAxis]];
                break;
              }
              case RYGAlignCenter: {
                float childHeight = child->layout.measuredDimensions[dim[crossAxis]];
                child->layout.position[pos[crossAxis]] =
                currentLead + (lineHeight - childHeight) / 2;
                break;
              }
              case RYGAlignStretch: {
                child->layout.position[pos[crossAxis]] =
                currentLead + RYGNodeLeadingMargin(child, crossAxis, availableInnerWidth);

                // Remeasure child with the line height as it as been only measured with the
                // parents height yet.
                if (!RYGNodeIsStyleDimDefined(child, crossAxis, availableInnerCrossDim)) {
                  const float childWidth =
                  isMainAxisRow ? (child->layout.measuredDimensions[RYGDimensionWidth] +
                                   RYGNodeMarginForAxis(child, crossAxis, availableInnerWidth))
                  : lineHeight;

                  const float childHeight =
                  !isMainAxisRow ? (child->layout.measuredDimensions[RYGDimensionHeight] +
                                    RYGNodeMarginForAxis(child, crossAxis, availableInnerWidth))
                  : lineHeight;

                  if (!(RYGFloatsEqual(childWidth,
                                      child->layout.measuredDimensions[RYGDimensionWidth]) &&
                        RYGFloatsEqual(childHeight,
                                      child->layout.measuredDimensions[RYGDimensionHeight]))) {
                          RYGLayoutNodeInternal(child,
                                               childWidth,
                                               childHeight,
                                               direction,
                                               RYGMeasureModeExactly,
                                               RYGMeasureModeExactly,
                                               availableInnerWidth,
                                               availableInnerHeight,
                                               true,
                                               "stretch");
                        }
                }
                break;
              }
              case RYGAlignBaseline: {
                child->layout.position[RYGEdgeTop] =
                currentLead + maxAscentForCurrentLine - RYGBaseline(child) +
                RYGNodeLeadingPosition(child, RYGFlexDirectionColumn, availableInnerCrossDim);
                break;
              }
              case RYGAlignAuto:
              case RYGAlignSpaceBetween:
              case RYGAlignSpaceAround:
                break;
            }
          }
        }
      }

      currentLead += lineHeight;
    }
  }

  // STEP 9: COMPUTING FINAL DIMENSIONS
  node->layout.measuredDimensions[RYGDimensionWidth] = RYGNodeBoundAxis(
                                                                      node, RYGFlexDirectionRow, availableWidth - marginAxisRow, parentWidth, parentWidth);
  node->layout.measuredDimensions[RYGDimensionHeight] = RYGNodeBoundAxis(
                                                                       node, RYGFlexDirectionColumn, availableHeight - marginAxisColumn, parentHeight, parentWidth);

  // If the user didn't specify a width or height for the node, set the
  // dimensions based on the children.
  if (measureModeMainDim == RYGMeasureModeUndefined ||
      (node->style.overflow != RYGOverflowScroll && measureModeMainDim == RYGMeasureModeAtMost)) {
    // Clamp the size to the min/max size, if specified, and make sure it
    // doesn't go below the padding and border amount.
    node->layout.measuredDimensions[dim[mainAxis]] =
    RYGNodeBoundAxis(node, mainAxis, maxLineMainDim, mainAxisParentSize, parentWidth);
  } else if (measureModeMainDim == RYGMeasureModeAtMost &&
             node->style.overflow == RYGOverflowScroll) {
    node->layout.measuredDimensions[dim[mainAxis]] = fmaxf(
                                                           fminf(availableInnerMainDim + paddingAndBorderAxisMain,
                                                                 RYGNodeBoundAxisWithinMinAndMax(node, mainAxis, maxLineMainDim, mainAxisParentSize)),
                                                           paddingAndBorderAxisMain);
  }

  if (measureModeCrossDim == RYGMeasureModeUndefined ||
      (node->style.overflow != RYGOverflowScroll && measureModeCrossDim == RYGMeasureModeAtMost)) {
    // Clamp the size to the min/max size, if specified, and make sure it
    // doesn't go below the padding and border amount.
    node->layout.measuredDimensions[dim[crossAxis]] =
    RYGNodeBoundAxis(node,
                    crossAxis,
                    totalLineCrossDim + paddingAndBorderAxisCross,
                    crossAxisParentSize,
                    parentWidth);
  } else if (measureModeCrossDim == RYGMeasureModeAtMost &&
             node->style.overflow == RYGOverflowScroll) {
    node->layout.measuredDimensions[dim[crossAxis]] =
    fmaxf(fminf(availableInnerCrossDim + paddingAndBorderAxisCross,
                RYGNodeBoundAxisWithinMinAndMax(node,
                                               crossAxis,
                                               totalLineCrossDim + paddingAndBorderAxisCross,
                                               crossAxisParentSize)),
          paddingAndBorderAxisCross);
  }

  // As we only wrapped in normal direction yet, we need to reverse the positions on wrap-reverse.
  if (performLayout && node->style.flexWrap == RYGWrapWrapReverse) {
    for (uint32_t i = 0; i < childCount; i++) {
      const RYGNodeRef child = RYGNodeGetChild(node, i);
      if (child->style.positionType == RYGPositionTypeRelative) {
        child->layout.position[pos[crossAxis]] = node->layout.measuredDimensions[dim[crossAxis]] -
        child->layout.position[pos[crossAxis]] -
        child->layout.measuredDimensions[dim[crossAxis]];
      }
    }
  }

  if (performLayout) {
    // STEP 10: SIZING AND POSITIONING ABSOLUTE CHILDREN
    for (currentAbsoluteChild = firstAbsoluteChild; currentAbsoluteChild != NULL;
         currentAbsoluteChild = currentAbsoluteChild->nextChild) {
      RYGNodeAbsoluteLayoutChild(node,
                                currentAbsoluteChild,
                                availableInnerWidth,
                                widthMeasureMode,
                                availableInnerHeight,
                                direction);
    }

    // STEP 11: SETTING TRAILING POSITIONS FOR CHILDREN
    const bool needsMainTrailingPos =
    mainAxis == RYGFlexDirectionRowReverse || mainAxis == RYGFlexDirectionColumnReverse;
    const bool needsCrossTrailingPos =
    crossAxis == RYGFlexDirectionRowReverse || crossAxis == RYGFlexDirectionColumnReverse;

    // Set trailing position if necessary.
    if (needsMainTrailingPos || needsCrossTrailingPos) {
      for (uint32_t i = 0; i < childCount; i++) {
        const RYGNodeRef child = RYGNodeListGet(node->children, i);
        if (child->style.display == RYGDisplayNone) {
          continue;
        }
        if (needsMainTrailingPos) {
          RYGNodeSetChildTrailingPosition(node, child, mainAxis);
        }

        if (needsCrossTrailingPos) {
          RYGNodeSetChildTrailingPosition(node, child, crossAxis);
        }
      }
    }
  }
}

uint32_t gDepth = 0;
bool gPrintTree = false;
bool gPrintChanges = false;
bool gPrintSkips = false;

static const char *spacer = "                                                            ";

static const char *RYGSpacer(const unsigned long level) {
  const size_t spacerLen = strlen(spacer);
  if (level > spacerLen) {
    return &spacer[0];
  } else {
    return &spacer[spacerLen - level];
  }
}

static const char *RYGMeasureModeName(const RYGMeasureMode mode, const bool performLayout) {
  const char *kMeasureModeNames[RYGMeasureModeCount] = {"UNDEFINED", "EXACTLY", "AT_MOST"};
  const char *kLayoutModeNames[RYGMeasureModeCount] = {"LAY_UNDEFINED",
    "LAY_EXACTLY",
    "LAY_AT_"
    "MOST"};

  if (mode >= RYGMeasureModeCount) {
    return "";
  }

  return performLayout ? kLayoutModeNames[mode] : kMeasureModeNames[mode];
}

static inline bool RYGMeasureModeSizeIsExactAndMatchesOldMeasuredSize(RYGMeasureMode sizeMode,
                                                                     float size,
                                                                     float lastComputedSize) {
  return sizeMode == RYGMeasureModeExactly && RYGFloatsEqual(size, lastComputedSize);
}

static inline bool RYGMeasureModeOldSizeIsUnspecifiedAndStillFits(RYGMeasureMode sizeMode,
                                                                 float size,
                                                                 RYGMeasureMode lastSizeMode,
                                                                 float lastComputedSize) {
  return sizeMode == RYGMeasureModeAtMost && lastSizeMode == RYGMeasureModeUndefined &&
  (size >= lastComputedSize || RYGFloatsEqual(size, lastComputedSize));
}

static inline bool RYGMeasureModeNewMeasureSizeIsStricterAndStillValid(RYGMeasureMode sizeMode,
                                                                      float size,
                                                                      RYGMeasureMode lastSizeMode,
                                                                      float lastSize,
                                                                      float lastComputedSize) {
  return lastSizeMode == RYGMeasureModeAtMost && sizeMode == RYGMeasureModeAtMost &&
  lastSize > size && (lastComputedSize <= size || RYGFloatsEqual(size, lastComputedSize));
}

bool RYGNodeCanUseCachedMeasurement(const RYGMeasureMode widthMode,
                                   const float width,
                                   const RYGMeasureMode heightMode,
                                   const float height,
                                   const RYGMeasureMode lastWidthMode,
                                   const float lastWidth,
                                   const RYGMeasureMode lastHeightMode,
                                   const float lastHeight,
                                   const float lastComputedWidth,
                                   const float lastComputedHeight,
                                   const float marginRow,
                                   const float marginColumn) {
  if (lastComputedHeight < 0 || lastComputedWidth < 0) {
    return false;
  }

  const bool hasSameWidthSpec = lastWidthMode == widthMode && RYGFloatsEqual(lastWidth, width);
  const bool hasSameHeightSpec = lastHeightMode == heightMode && RYGFloatsEqual(lastHeight, height);

  const bool widthIsCompatible =
  hasSameWidthSpec || RYGMeasureModeSizeIsExactAndMatchesOldMeasuredSize(widthMode,
                                                                        width - marginRow,
                                                                        lastComputedWidth) ||
  RYGMeasureModeOldSizeIsUnspecifiedAndStillFits(widthMode,
                                                width - marginRow,
                                                lastWidthMode,
                                                lastComputedWidth) ||
  RYGMeasureModeNewMeasureSizeIsStricterAndStillValid(
                                                     widthMode, width - marginRow, lastWidthMode, lastWidth, lastComputedWidth);

  const bool heightIsCompatible =
  hasSameHeightSpec || RYGMeasureModeSizeIsExactAndMatchesOldMeasuredSize(heightMode,
                                                                         height - marginColumn,
                                                                         lastComputedHeight) ||
  RYGMeasureModeOldSizeIsUnspecifiedAndStillFits(heightMode,
                                                height - marginColumn,
                                                lastHeightMode,
                                                lastComputedHeight) ||
  RYGMeasureModeNewMeasureSizeIsStricterAndStillValid(
                                                     heightMode, height - marginColumn, lastHeightMode, lastHeight, lastComputedHeight);

  return widthIsCompatible && heightIsCompatible;
}

//
// This is a wrapper around the RYGNodelayoutImpl function. It determines
// whether the layout request is redundant and can be skipped.
//
// Parameters:
//  Input parameters are the same as RYGNodelayoutImpl (see above)
//  Return parameter is true if layout was performed, false if skipped
//
bool RYGLayoutNodeInternal(const RYGNodeRef node,
                          const float availableWidth,
                          const float availableHeight,
                          const RYGDirection parentDirection,
                          const RYGMeasureMode widthMeasureMode,
                          const RYGMeasureMode heightMeasureMode,
                          const float parentWidth,
                          const float parentHeight,
                          const bool performLayout,
                          const char *reason) {
  RYGLayout *layout = &node->layout;

  gDepth++;

  const bool needToVisitNode =
  (node->isDirty && layout->generationCount != gCurrentGenerationCount) ||
  layout->lastParentDirection != parentDirection;

  if (needToVisitNode) {
    // Invalidate the cached results.
    layout->nextCachedMeasurementsIndex = 0;
    layout->cachedLayout.widthMeasureMode = (RYGMeasureMode) -1;
    layout->cachedLayout.heightMeasureMode = (RYGMeasureMode) -1;
    layout->cachedLayout.computedWidth = -1;
    layout->cachedLayout.computedHeight = -1;
  }

  RYGCachedMeasurement *cachedResults = NULL;

  // Determine whether the results are already cached. We maintain a separate
  // cache for layouts and measurements. A layout operation modifies the
  // positions
  // and dimensions for nodes in the subtree. The algorithm assumes that each
  // node
  // gets layed out a maximum of one time per tree layout, but multiple
  // measurements
  // may be required to resolve all of the flex dimensions.
  // We handle nodes with measure functions specially here because they are the
  // most
  // expensive to measure, so it's worth avoiding redundant measurements if at
  // all possible.
  if (node->measure) {
    const float marginAxisRow = RYGNodeMarginForAxis(node, RYGFlexDirectionRow, parentWidth);
    const float marginAxisColumn = RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, parentWidth);

    // First, try to use the layout cache.
    if (RYGNodeCanUseCachedMeasurement(widthMeasureMode,
                                      availableWidth,
                                      heightMeasureMode,
                                      availableHeight,
                                      layout->cachedLayout.widthMeasureMode,
                                      layout->cachedLayout.availableWidth,
                                      layout->cachedLayout.heightMeasureMode,
                                      layout->cachedLayout.availableHeight,
                                      layout->cachedLayout.computedWidth,
                                      layout->cachedLayout.computedHeight,
                                      marginAxisRow,
                                      marginAxisColumn)) {
      cachedResults = &layout->cachedLayout;
    } else {
      // Try to use the measurement cache.
      for (uint32_t i = 0; i < layout->nextCachedMeasurementsIndex; i++) {
        if (RYGNodeCanUseCachedMeasurement(widthMeasureMode,
                                          availableWidth,
                                          heightMeasureMode,
                                          availableHeight,
                                          layout->cachedMeasurements[i].widthMeasureMode,
                                          layout->cachedMeasurements[i].availableWidth,
                                          layout->cachedMeasurements[i].heightMeasureMode,
                                          layout->cachedMeasurements[i].availableHeight,
                                          layout->cachedMeasurements[i].computedWidth,
                                          layout->cachedMeasurements[i].computedHeight,
                                          marginAxisRow,
                                          marginAxisColumn)) {
          cachedResults = &layout->cachedMeasurements[i];
          break;
        }
      }
    }
  } else if (performLayout) {
    if (RYGFloatsEqual(layout->cachedLayout.availableWidth, availableWidth) &&
        RYGFloatsEqual(layout->cachedLayout.availableHeight, availableHeight) &&
        layout->cachedLayout.widthMeasureMode == widthMeasureMode &&
        layout->cachedLayout.heightMeasureMode == heightMeasureMode) {
      cachedResults = &layout->cachedLayout;
    }
  } else {
    for (uint32_t i = 0; i < layout->nextCachedMeasurementsIndex; i++) {
      if (RYGFloatsEqual(layout->cachedMeasurements[i].availableWidth, availableWidth) &&
          RYGFloatsEqual(layout->cachedMeasurements[i].availableHeight, availableHeight) &&
          layout->cachedMeasurements[i].widthMeasureMode == widthMeasureMode &&
          layout->cachedMeasurements[i].heightMeasureMode == heightMeasureMode) {
        cachedResults = &layout->cachedMeasurements[i];
        break;
      }
    }
  }

  if (!needToVisitNode && cachedResults != NULL) {
    layout->measuredDimensions[RYGDimensionWidth] = cachedResults->computedWidth;
    layout->measuredDimensions[RYGDimensionHeight] = cachedResults->computedHeight;

    if (gPrintChanges && gPrintSkips) {
      printf("%s%d.{[skipped] ", RYGSpacer(gDepth), gDepth);
      if (node->print) {
        node->print(node);
      }
      printf("wm: %s, hm: %s, aw: %f ah: %f => d: (%f, %f) %s\n",
             RYGMeasureModeName(widthMeasureMode, performLayout),
             RYGMeasureModeName(heightMeasureMode, performLayout),
             availableWidth,
             availableHeight,
             cachedResults->computedWidth,
             cachedResults->computedHeight,
             reason);
    }
  } else {
    if (gPrintChanges) {
      printf("%s%d.{%s", RYGSpacer(gDepth), gDepth, needToVisitNode ? "*" : "");
      if (node->print) {
        node->print(node);
      }
      printf("wm: %s, hm: %s, aw: %f ah: %f %s\n",
             RYGMeasureModeName(widthMeasureMode, performLayout),
             RYGMeasureModeName(heightMeasureMode, performLayout),
             availableWidth,
             availableHeight,
             reason);
    }

    RYGNodelayoutImpl(node,
                     availableWidth,
                     availableHeight,
                     parentDirection,
                     widthMeasureMode,
                     heightMeasureMode,
                     parentWidth,
                     parentHeight,
                     performLayout);

    if (gPrintChanges) {
      printf("%s%d.}%s", RYGSpacer(gDepth), gDepth, needToVisitNode ? "*" : "");
      if (node->print) {
        node->print(node);
      }
      printf("wm: %s, hm: %s, d: (%f, %f) %s\n",
             RYGMeasureModeName(widthMeasureMode, performLayout),
             RYGMeasureModeName(heightMeasureMode, performLayout),
             layout->measuredDimensions[RYGDimensionWidth],
             layout->measuredDimensions[RYGDimensionHeight],
             reason);
    }

    layout->lastParentDirection = parentDirection;

    if (cachedResults == NULL) {
      if (layout->nextCachedMeasurementsIndex == RYG_MAX_CACHED_RESULT_COUNT) {
        if (gPrintChanges) {
          printf("Out of cache entries!\n");
        }
        layout->nextCachedMeasurementsIndex = 0;
      }

      RYGCachedMeasurement *newCacheEntry;
      if (performLayout) {
        // Use the single layout cache entry.
        newCacheEntry = &layout->cachedLayout;
      } else {
        // Allocate a new measurement cache entry.
        newCacheEntry = &layout->cachedMeasurements[layout->nextCachedMeasurementsIndex];
        layout->nextCachedMeasurementsIndex++;
      }

      newCacheEntry->availableWidth = availableWidth;
      newCacheEntry->availableHeight = availableHeight;
      newCacheEntry->widthMeasureMode = widthMeasureMode;
      newCacheEntry->heightMeasureMode = heightMeasureMode;
      newCacheEntry->computedWidth = layout->measuredDimensions[RYGDimensionWidth];
      newCacheEntry->computedHeight = layout->measuredDimensions[RYGDimensionHeight];
    }
  }

  if (performLayout) {
    node->layout.dimensions[RYGDimensionWidth] = node->layout.measuredDimensions[RYGDimensionWidth];
    node->layout.dimensions[RYGDimensionHeight] = node->layout.measuredDimensions[RYGDimensionHeight];
    node->hasNewLayout = true;
    node->isDirty = false;
  }

  gDepth--;
  layout->generationCount = gCurrentGenerationCount;
  return (needToVisitNode || cachedResults == NULL);
}

static void RYGRoundToPixelGrid(const RYGNodeRef node) {
  const float fractialLeft =
  node->layout.position[RYGEdgeLeft] - floorf(node->layout.position[RYGEdgeLeft]);
  const float fractialTop =
  node->layout.position[RYGEdgeTop] - floorf(node->layout.position[RYGEdgeTop]);
  node->layout.dimensions[RYGDimensionWidth] =
  roundf(fractialLeft + node->layout.dimensions[RYGDimensionWidth]) - roundf(fractialLeft);
  node->layout.dimensions[RYGDimensionHeight] =
  roundf(fractialTop + node->layout.dimensions[RYGDimensionHeight]) - roundf(fractialTop);

  node->layout.position[RYGEdgeLeft] = roundf(node->layout.position[RYGEdgeLeft]);
  node->layout.position[RYGEdgeTop] = roundf(node->layout.position[RYGEdgeTop]);

  const uint32_t childCount = RYGNodeListCount(node->children);
  for (uint32_t i = 0; i < childCount; i++) {
    RYGRoundToPixelGrid(RYGNodeGetChild(node, i));
  }
}

void RYGNodeCalculateLayout(const RYGNodeRef node,
                           const float availableWidth,
                           const float availableHeight,
                           const RYGDirection parentDirection) {
  // Increment the generation count. This will force the recursive routine to
  // visit
  // all dirty nodes at least once. Subsequent visits will be skipped if the
  // input
  // parameters don't change.
  gCurrentGenerationCount++;

  float width = availableWidth;
  float height = availableHeight;
  RYGMeasureMode widthMeasureMode = RYGMeasureModeUndefined;
  RYGMeasureMode heightMeasureMode = RYGMeasureModeUndefined;

  RYGResolveDimensions(node);

  if (!RYGFloatIsUndefined(width)) {
    widthMeasureMode = RYGMeasureModeExactly;
  } else if (RYGNodeIsStyleDimDefined(node, RYGFlexDirectionRow, availableWidth)) {
    width = RYGValueResolve(node->resolvedDimensions[dim[RYGFlexDirectionRow]], availableWidth) +
    RYGNodeMarginForAxis(node, RYGFlexDirectionRow, availableWidth);
    widthMeasureMode = RYGMeasureModeExactly;
  } else if (RYGValueResolve(&node->style.maxDimensions[RYGDimensionWidth], availableWidth) >= 0.0f) {
    width = RYGValueResolve(&node->style.maxDimensions[RYGDimensionWidth], availableWidth);
    widthMeasureMode = RYGMeasureModeAtMost;
  }

  if (!RYGFloatIsUndefined(height)) {
    heightMeasureMode = RYGMeasureModeExactly;
  } else if (RYGNodeIsStyleDimDefined(node, RYGFlexDirectionColumn, availableHeight)) {
    height = RYGValueResolve(node->resolvedDimensions[dim[RYGFlexDirectionColumn]], availableHeight) +
    RYGNodeMarginForAxis(node, RYGFlexDirectionColumn, availableWidth);
    heightMeasureMode = RYGMeasureModeExactly;
  } else if (RYGValueResolve(&node->style.maxDimensions[RYGDimensionHeight], availableHeight) >=
             0.0f) {
    height = RYGValueResolve(&node->style.maxDimensions[RYGDimensionHeight], availableHeight);
    heightMeasureMode = RYGMeasureModeAtMost;
  }

  if (RYGLayoutNodeInternal(node,
                           width,
                           height,
                           parentDirection,
                           widthMeasureMode,
                           heightMeasureMode,
                           availableWidth,
                           availableHeight,
                           true,
                           "initia"
                           "l")) {
    RYGNodeSetPosition(node, node->layout.direction, availableWidth, availableHeight, availableWidth);

    if (RYGIsExperimentalFeatureEnabled(RYGExperimentalFeatureRounding)) {
      RYGRoundToPixelGrid(node);
    }

    if (gPrintTree) {
      RYGNodePrint(node, RYGPrintOptionsLayout | RYGPrintOptionsChildren | RYGPrintOptionsStyle);
    }
  }
}

void RYGSetLogger(RYGLogger logger) {
  gLogger = logger;
}

void RYGLog(RYGLogLevel level, const char *format, ...) {
  va_list args;
  va_start(args, format);
  gLogger(level, format, args);
  va_end(args);
}

static bool experimentalFeatures[RYGExperimentalFeatureCount + 1];

void RYGSetExperimentalFeatureEnabled(RYGExperimentalFeature feature, bool enabled) {
  experimentalFeatures[feature] = enabled;
}

inline bool RYGIsExperimentalFeatureEnabled(RYGExperimentalFeature feature) {
  return experimentalFeatures[feature];
}

void RYGSetMemoryFuncs(RYGMalloc RYGmalloc, RYGCalloc yccalloc, RYGRealloc RYGrealloc, RYGFree RYGfree) {
  RYG_ASSERT(gNodeInstanceCount == 0, "Cannot set memory functions: all node must be freed first");
  RYG_ASSERT((RYGmalloc == NULL && yccalloc == NULL && RYGrealloc == NULL && RYGfree == NULL) ||
            (RYGmalloc != NULL && yccalloc != NULL && RYGrealloc != NULL && RYGfree != NULL),
            "Cannot set memory functions: functions must be all NULL or Non-NULL");

  if (RYGmalloc == NULL || yccalloc == NULL || RYGrealloc == NULL || RYGfree == NULL) {
    gRYGMalloc = &malloc;
    gRYGCalloc = &calloc;
    gRYGRealloc = &realloc;
    gRYGFree = &free;
  } else {
    gRYGMalloc = RYGmalloc;
    gRYGCalloc = yccalloc;
    gRYGRealloc = RYGrealloc;
    gRYGFree = RYGfree;
  }
}
