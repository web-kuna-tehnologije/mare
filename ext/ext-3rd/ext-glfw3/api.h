#if CODE
#pragma once
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#if V3_WINDOWS
#include <winsock2.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif V3_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_NSGL
#elif V3_LINUX
//#define GLFW_EXPOSE_NATIVE_X11 // also, GLFW_EXPOSE_NATIVE_WAYLAND
//#define GLFW_EXPOSE_NATIVE_GLX

//#define GLFW_EXPOSE_NATIVE_EGL
//#define GLFW_EXPOSE_NATIVE_OSMESA
#endif

#include "include/GLFW/glfw3.h"
#include "include/GLFW/glfw3native.h"

#if CODE
#pragma once
#undef  GLFW_INCLUDE_NONE
#if V3_MACOS
#define _GLFW_COCOA
#elif V3_WINDOWS
#define _GLFW_WIN32
#elif V3_LINUX
#define _GLFW_X11 // also, _GLFW_OSMESA or _GLFW_WAYLAND
#endif
#include "glob.c"
#if defined _MSC_VER
#pragma comment(lib, "user32")
#endif // MSVC-only: clang on linux fails on dependent-library specifiers
#if defined _MSC_VER
#pragma comment(lib, "gdi32")
#endif // MSVC-only: clang on linux fails on dependent-library specifiers
#if defined _MSC_VER
#pragma comment(lib, "shell32")
#endif // MSVC-only: clang on linux fails on dependent-library specifiers
#endif
