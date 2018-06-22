/** Copyright (c) 2014-present, Facebook, Inc. */

#pragma once

#ifdef __cplusplus
#define RYG_EXTERN_C_BEGIN extern "C" {
#define RYG_EXTERN_C_END }
#else
#define RYG_EXTERN_C_BEGIN
#define RYG_EXTERN_C_END
#endif

#ifdef _WINDLL
#define WIN_EXPORT __declspec(dllexport)
#else
#define WIN_EXPORT
#endif

#ifndef FB_ASSERTIONS_ENABLED
#define FB_ASSERTIONS_ENABLED 1
#endif

#if FB_ASSERTIONS_ENABLED
#define RYG_ABORT() abort()
#else
#define RYG_ABORT()
#endif

#ifndef RYG_ASSERT
#define RYG_ASSERT(X, message)              \
if (!(X)) {                              \
RYGLog(RYGLogLevelError, "%s", message); \
RYG_ABORT();                            \
}
#endif

#ifdef NS_ENUM
// Cannot use NSInteger as NSInteger has a different size than int (which is the default type of a
// enum).
// Therefor when linking the Yoga C library into obj-c the header is a missmatch for the Yoga ABI.
#define RYG_ENUM_BEGIN(name) NS_ENUM(int, name)
#define RYG_ENUM_END(name)
#else
#define RYG_ENUM_BEGIN(name) enum name
#define RYG_ENUM_END(name) name
#endif
