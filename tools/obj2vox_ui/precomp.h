// Template, IGAD version 2
// IGAD/NHTV/UU - Jacco Bikker - 2006-2020

// add your includes to this file instead of to individual .cpp files
// to enjoy the benefits of precompiled headers:
// - fast compilation
// - solve issues with the order of header files once (here)
// do not include headers in header files (ever).

// Default screen resolution
#define SCRWIDTH 1024
#define SCRHEIGHT 700

// C++ headers
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <math.h>

// Headers for Dear ImGui
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <ctype.h>
#include <limits.h>
#include <math.h> 
#include <stdio.h>
#if !defined(alloca)
#if defined(__GLIBC__) || defined(__CYGWIN__) || defined(__APPLE__) || defined(__SWITCH__)
#include <alloca.h>     // alloca (glibc uses <alloca.h>)
#elif defined(_WIN32)
#include <malloc.h>     // alloca
#if !defined(alloca)
#define alloca _alloca  // for clang with MS Codegen
#endif
#else
#include <stdlib.h>     // alloca
#endif
#endif
#include <stdint.h>     // intptr_t
#include <assert.h>

// Header for AVX, and every technology before it.
// If your CPU does not support this (unlikely), include the appropriate header instead.
// See: https://stackoverflow.com/a/11228864/2844473
#include <immintrin.h>

// clang-format off

// "Leak" common namespaces to all compilation units. This is not standard
// C++ practice but a simplification for template projects.
using namespace std;

// Windows
#ifdef _WINDOWS
#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

// GLFW
#define GLFW_USE_CHDIR 0
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// constants
#define PI			3.14159265358979323846264f
#define INVPI		0.31830988618379067153777f
#define INV2PI		0.15915494309189533576888f
#define TWOPI		6.28318530717958647692528f
#define SQRT_PI_INV	0.56418958355f
#define LARGE_FLOAT	1e34f

// Tool includes
#include <FreeImage.h>			// image loading. http://freeimage.sourceforge.net
#include <zlib.h>				// compression. https://www.zlib.net
#include <half.hpp>				// half floats. http://half.sourceforge.net

// Template headers
#include "surface.h"			// pixel surface class
#include "template.h"			// template functionality

// Namespaces
using namespace half_float;
using namespace Tmpl8;

// Add your headers here; they will be able to use all previously defined classes and namespaces.
// In your own .cpp files just add #include "precomp.h".
// #include "my_include.h"

// Game
#include "scene.h"
#include "game.h"				// game class

// clang-format on

// EOF