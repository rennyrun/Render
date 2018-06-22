/** Copyright (c) 2014-present, Facebook, Inc. */

#pragma once

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

// Not defined in MSVC++
#ifndef NAN
static const unsigned long __nan[2] = {0xffffffff, 0x7fffffff};
#define NAN (*(const float *) __nan)
#endif

#define RYGUndefined NAN


#include "YGEnums.h"
#include "YGMacros.h"

RYG_EXTERN_C_BEGIN

typedef struct RYGSize {
  float width;
  float height;
} RYGSize;

typedef struct RYGValue {
  float value;
  RYGUnit unit;
} RYGValue;

static const RYGValue RYGValueUndefined = {RYGUndefined, RYGUnitUndefined};
static const RYGValue RYGValueAuto = {RYGUndefined, RYGUnitAuto};

typedef struct RYGNode *RYGNodeRef;
typedef RYGSize (*RYGMeasureFunc)(RYGNodeRef node,
float width,
RYGMeasureMode widthMode,
float height,
RYGMeasureMode heightMode);
typedef float (*RYGBaselineFunc)(RYGNodeRef node, const float width, const float height);
typedef void (*RYGPrintFunc)(RYGNodeRef node);
typedef int (*RYGLogger)(RYGLogLevel level, const char *format, va_list args);

typedef void *(*RYGMalloc)(size_t size);
typedef void *(*RYGCalloc)(size_t count, size_t size);
typedef void *(*RYGRealloc)(void *ptr, size_t size);
typedef void (*RYGFree)(void *ptr);

// RYGNode
WIN_EXPORT RYGNodeRef RYGNodeNew(void);
WIN_EXPORT void RYGNodeFree(const RYGNodeRef node);
WIN_EXPORT void RYGNodeFreeRecursive(const RYGNodeRef node);
WIN_EXPORT void RYGNodeReset(const RYGNodeRef node);
WIN_EXPORT int32_t RYGNodeGetInstanceCount(void);

WIN_EXPORT void RYGNodeInsertChild(const RYGNodeRef node,
                                  const RYGNodeRef child,
                                  const uint32_t index);
WIN_EXPORT void RYGNodeRemoveChild(const RYGNodeRef node, const RYGNodeRef child);
WIN_EXPORT RYGNodeRef RYGNodeGetChild(const RYGNodeRef node, const uint32_t index);
WIN_EXPORT RYGNodeRef RYGNodeGetParent(const RYGNodeRef node);
WIN_EXPORT uint32_t RYGNodeGetChildCount(const RYGNodeRef node);

WIN_EXPORT void RYGNodeCalculateLayout(const RYGNodeRef node,
                                      const float availableWidth,
                                      const float availableHeight,
                                      const RYGDirection parentDirection);

// Mark a node as dirty. Only valid for nodes with a custom measure function
// set.
// RYG knows when to mark all other nodes as dirty but because nodes with
// measure functions
// depends on information not known to RYG they must perform this dirty
// marking manually.
WIN_EXPORT void RYGNodeMarkDirty(const RYGNodeRef node);
WIN_EXPORT bool RYGNodeIsDirty(const RYGNodeRef node);

WIN_EXPORT void RYGNodePrint(const RYGNodeRef node, const RYGPrintOptions options);

WIN_EXPORT bool RYGFloatIsUndefined(const float value);

WIN_EXPORT bool RYGNodeCanUseCachedMeasurement(const RYGMeasureMode widthMode,
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
                                              const float marginColumn);

WIN_EXPORT void RYGNodeCopyStyle(const RYGNodeRef dstNode, const RYGNodeRef srcNode);

#define RYG_NODE_PROPERTY(type, name, paramName)                          \
WIN_EXPORT void RYGNodeSet##name(const RYGNodeRef node, type paramName); \
WIN_EXPORT type RYGNodeGet##name(const RYGNodeRef node);

#define RYG_NODE_STYLE_PROPERTY(type, name, paramName)                               \
WIN_EXPORT void RYGNodeStyleSet##name(const RYGNodeRef node, const type paramName); \
WIN_EXPORT type RYGNodeStyleGet##name(const RYGNodeRef node);

#define RYG_NODE_STYLE_PROPERTY_UNIT(type, name, paramName)                                    \
WIN_EXPORT void RYGNodeStyleSet##name(const RYGNodeRef node, const float paramName);          \
WIN_EXPORT void RYGNodeStyleSet##name##Percent(const RYGNodeRef node, const float paramName); \
WIN_EXPORT type RYGNodeStyleGet##name(const RYGNodeRef node);

#define RYG_NODE_STYLE_PROPERTY_UNIT_AUTO(type, name, paramName) \
RYG_NODE_STYLE_PROPERTY_UNIT(type, name, paramName)            \
WIN_EXPORT void RYGNodeStyleSet##name##Auto(const RYGNodeRef node);

#define RYG_NODE_STYLE_EDGE_PROPERTY(type, name, paramName)    \
WIN_EXPORT void RYGNodeStyleSet##name(const RYGNodeRef node,  \
const RYGEdge edge,     \
const type paramName); \
WIN_EXPORT type RYGNodeStyleGet##name(const RYGNodeRef node, const RYGEdge edge);

#define RYG_NODE_STYLE_EDGE_PROPERTY_UNIT(type, name, paramName)         \
WIN_EXPORT void RYGNodeStyleSet##name(const RYGNodeRef node,            \
const RYGEdge edge,               \
const float paramName);          \
WIN_EXPORT void RYGNodeStyleSet##name##Percent(const RYGNodeRef node,   \
const RYGEdge edge,      \
const float paramName); \
WIN_EXPORT type RYGNodeStyleGet##name(const RYGNodeRef node, const RYGEdge edge);

#define RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_AUTO(type, name) \
WIN_EXPORT void RYGNodeStyleSet##name##Auto(const RYGNodeRef node, const RYGEdge edge);

#define RYG_NODE_LAYOUT_PROPERTY(type, name) \
WIN_EXPORT type RYGNodeLayoutGet##name(const RYGNodeRef node);

#define RYG_NODE_LAYOUT_EDGE_PROPERTY(type, name) \
WIN_EXPORT type RYGNodeLayoutGet##name(const RYGNodeRef node, const RYGEdge edge);

RYG_NODE_PROPERTY(void *, Context, context);
RYG_NODE_PROPERTY(RYGMeasureFunc, MeasureFunc, measureFunc);
RYG_NODE_PROPERTY(RYGBaselineFunc, BaselineFunc, baselineFunc)
RYG_NODE_PROPERTY(RYGPrintFunc, PrintFunc, printFunc);
RYG_NODE_PROPERTY(bool, HasNewLayout, hasNewLayout);

RYG_NODE_STYLE_PROPERTY(RYGDirection, Direction, direction);
RYG_NODE_STYLE_PROPERTY(RYGFlexDirection, FlexDirection, flexDirection);
RYG_NODE_STYLE_PROPERTY(RYGJustify, JustifyContent, justifyContent);
RYG_NODE_STYLE_PROPERTY(RYGAlign, AlignContent, alignContent);
RYG_NODE_STYLE_PROPERTY(RYGAlign, AlignItems, alignItems);
RYG_NODE_STYLE_PROPERTY(RYGAlign, AlignSelf, alignSelf);
RYG_NODE_STYLE_PROPERTY(RYGPositionType, PositionType, positionType);
RYG_NODE_STYLE_PROPERTY(RYGWrap, FlexWrap, flexWrap);
RYG_NODE_STYLE_PROPERTY(RYGOverflow, Overflow, overflow);
RYG_NODE_STYLE_PROPERTY(RYGDisplay, Display, display);

WIN_EXPORT void RYGNodeStyleSetFlex(const RYGNodeRef node, const float flex);
RYG_NODE_STYLE_PROPERTY(float, FlexGrow, flexGrow);
RYG_NODE_STYLE_PROPERTY(float, FlexShrink, flexShrink);
RYG_NODE_STYLE_PROPERTY_UNIT_AUTO(RYGValue, FlexBasis, flexBasis);

RYG_NODE_STYLE_EDGE_PROPERTY_UNIT(RYGValue, Position, position);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT(RYGValue, Margin, margin);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT_AUTO(RYGValue, Margin);
RYG_NODE_STYLE_EDGE_PROPERTY_UNIT(RYGValue, Padding, padding);
RYG_NODE_STYLE_EDGE_PROPERTY(float, Border, border);

RYG_NODE_STYLE_PROPERTY_UNIT_AUTO(RYGValue, Width, width);
RYG_NODE_STYLE_PROPERTY_UNIT_AUTO(RYGValue, Height, height);
RYG_NODE_STYLE_PROPERTY_UNIT(RYGValue, MinWidth, minWidth);
RYG_NODE_STYLE_PROPERTY_UNIT(RYGValue, MinHeight, minHeight);
RYG_NODE_STYLE_PROPERTY_UNIT(RYGValue, MaxWidth, maxWidth);
RYG_NODE_STYLE_PROPERTY_UNIT(RYGValue, MaxHeight, maxHeight);

// Yoga specific properties, not compatible with flexbox specification
// Aspect ratio control the size of the undefined dimension of a node.
// Aspect ratio is encoded as a floating point value width/height. e.g. A value of 2 leads to a node
// with a width twice the size of its height while a value of 0.5 gives the opposite effect.
//
// - On a node with a set width/height aspect ratio control the size of the unset dimension
// - On a node with a set flex basis aspect ratio controls the size of the node in the cross axis if
// unset
// - On a node with a measure function aspect ratio works as though the measure function measures
// the flex basis
// - On a node with flex grow/shrink aspect ratio controls the size of the node in the cross axis if
// unset
// - Aspect ratio takes min/max dimensions into account
RYG_NODE_STYLE_PROPERTY(float, AspectRatio, aspectRatio);

RYG_NODE_LAYOUT_PROPERTY(float, Left);
RYG_NODE_LAYOUT_PROPERTY(float, Top);
RYG_NODE_LAYOUT_PROPERTY(float, Right);
RYG_NODE_LAYOUT_PROPERTY(float, Bottom);
RYG_NODE_LAYOUT_PROPERTY(float, Width);
RYG_NODE_LAYOUT_PROPERTY(float, Height);
RYG_NODE_LAYOUT_PROPERTY(RYGDirection, Direction);

// Get the computed values for these nodes after performing layout. If they were set using
// point values then the returned value will be the same as RYGNodeStyleGetXXX. However if
// they were set using a percentage value then the returned value is the computed value used
// during layout.
RYG_NODE_LAYOUT_EDGE_PROPERTY(float, Margin);
RYG_NODE_LAYOUT_EDGE_PROPERTY(float, Border);
RYG_NODE_LAYOUT_EDGE_PROPERTY(float, Padding);

WIN_EXPORT void RYGSetLogger(RYGLogger logger);
WIN_EXPORT void RYGLog(RYGLogLevel level, const char *message, ...);

WIN_EXPORT void RYGSetExperimentalFeatureEnabled(RYGExperimentalFeature feature, bool enabled);
WIN_EXPORT bool RYGIsExperimentalFeatureEnabled(RYGExperimentalFeature feature);

WIN_EXPORT void
RYGSetMemoryFuncs(RYGMalloc RYGmalloc, RYGCalloc yccalloc, RYGRealloc RYGrealloc, RYGFree RYGfree);

RYG_EXTERN_C_END
