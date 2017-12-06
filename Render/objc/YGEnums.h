/** Copyright (c) 2014-present, Facebook, Inc. */

#pragma once

#include "YGMacros.h"

RYG_EXTERN_C_BEGIN

#define RYGAlignCount 8
typedef RYG_ENUM_BEGIN(RYGAlign) {
  RYGAlignAuto,
  RYGAlignFlexStart,
  RYGAlignCenter,
  RYGAlignFlexEnd,
  RYGAlignStretch,
  RYGAlignBaseline,
  RYGAlignSpaceBetween,
  RYGAlignSpaceAround,
} RYG_ENUM_END(RYGAlign);

#define RYGDimensionCount 2
typedef RYG_ENUM_BEGIN(RYGDimension) {
  RYGDimensionWidth,
  RYGDimensionHeight,
} RYG_ENUM_END(RYGDimension);

#define RYGDirectionCount 3
typedef RYG_ENUM_BEGIN(RYGDirection) {
  RYGDirectionInherit,
  RYGDirectionLTR,
  RYGDirectionRTL,
} RYG_ENUM_END(RYGDirection);

#define RYGDisplayCount 2
typedef RYG_ENUM_BEGIN(RYGDisplay) {
  RYGDisplayFlex,
  RYGDisplayNone,
} RYG_ENUM_END(RYGDisplay);

#define RYGEdgeCount 9
typedef RYG_ENUM_BEGIN(RYGEdge) {
  RYGEdgeLeft,
  RYGEdgeTop,
  RYGEdgeRight,
  RYGEdgeBottom,
  RYGEdgeStart,
  RYGEdgeEnd,
  RYGEdgeHorizontal,
  RYGEdgeVertical,
  RYGEdgeAll,
} RYG_ENUM_END(RYGEdge);

#define RYGExperimentalFeatureCount 2
typedef RYG_ENUM_BEGIN(RYGExperimentalFeature) {
  RYGExperimentalFeatureRounding,
  RYGExperimentalFeatureWebFlexBasis,
} RYG_ENUM_END(RYGExperimentalFeature);

#define RYGFlexDirectionCount 4
typedef RYG_ENUM_BEGIN(RYGFlexDirection) {
  RYGFlexDirectionColumn,
  RYGFlexDirectionColumnReverse,
  RYGFlexDirectionRow,
  RYGFlexDirectionRowReverse,
} RYG_ENUM_END(RYGFlexDirection);

#define RYGJustifyCount 5
typedef RYG_ENUM_BEGIN(RYGJustify) {
  RYGJustifyFlexStart,
  RYGJustifyCenter,
  RYGJustifyFlexEnd,
  RYGJustifySpaceBetween,
  RYGJustifySpaceAround,
} RYG_ENUM_END(RYGJustify);

#define RYGLogLevelCount 5
typedef RYG_ENUM_BEGIN(RYGLogLevel) {
  RYGLogLevelError,
  RYGLogLevelWarn,
  RYGLogLevelInfo,
  RYGLogLevelDebug,
  RYGLogLevelVerbose,
} RYG_ENUM_END(RYGLogLevel);

#define RYGMeasureModeCount 3
typedef RYG_ENUM_BEGIN(RYGMeasureMode) {
  RYGMeasureModeUndefined,
  RYGMeasureModeExactly,
  RYGMeasureModeAtMost,
} RYG_ENUM_END(RYGMeasureMode);

#define RYGOverflowCount 3
typedef RYG_ENUM_BEGIN(RYGOverflow) {
  RYGOverflowVisible,
  RYGOverflowHidden,
  RYGOverflowScroll,
} RYG_ENUM_END(RYGOverflow);

#define RYGPositionTypeCount 2
typedef RYG_ENUM_BEGIN(RYGPositionType) {
  RYGPositionTypeRelative,
  RYGPositionTypeAbsolute,
} RYG_ENUM_END(RYGPositionType);

#define RYGPrintOptionsCount 3
typedef RYG_ENUM_BEGIN(RYGPrintOptions) {
  RYGPrintOptionsLayout = 1,
  RYGPrintOptionsStyle = 2,
  RYGPrintOptionsChildren = 4,
} RYG_ENUM_END(RYGPrintOptions);

#define RYGUnitCount 4
typedef RYG_ENUM_BEGIN(RYGUnit) {
  RYGUnitUndefined,
  RYGUnitPoint,
  RYGUnitPercent,
  RYGUnitAuto,
} RYG_ENUM_END(RYGUnit);

#define RYGWrapCount 3
typedef RYG_ENUM_BEGIN(RYGWrap) {
  RYGWrapNoWrap,
  RYGWrapWrap,
  RYGWrapWrapReverse,
} RYG_ENUM_END(RYGWrap);

RYG_EXTERN_C_END
