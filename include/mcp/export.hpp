#pragma once

/**
 * @file export.hpp
 * @brief Export/import macros for DLL builds on Windows.
 *
 * This header provides macros for marking symbols for export when building
 * the DLL and for import when consuming the DLL.
 */

#if defined(_WIN32) || defined(__CYGWIN__)

#ifdef MCP_SDK_SHARED_LIBRARY

#ifdef MCP_SDK_BUILDING_LIBRARY
#define MCP_SDK_EXPORT __declspec(dllexport)
#else
#define MCP_SDK_EXPORT __declspec(dllimport)
#endif

#define MCP_SDK_PRIVATE
#define MCP_SDK_PUBLIC

#else

#define MCP_SDK_EXPORT
#define MCP_SDK_PRIVATE
#define MCP_SDK_PUBLIC

#endif

#else

#if __GNUC__ >= 4
#define MCP_SDK_EXPORT __attribute__((visibility("default")))
#define MCP_SDK_PRIVATE __attribute__((visibility("hidden")))
#define MCP_SDK_PUBLIC __attribute__((visibility("default")))
#else
#define MCP_SDK_EXPORT
#define MCP_SDK_PRIVATE
#define MCP_SDK_PUBLIC
#endif

#endif
