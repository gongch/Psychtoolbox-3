/*
  Psychtoolbox3/Source/Common/PsychIncludes.h

  AUTHORS:

  Allen.Ingling@nyu.edu         awi
  mario.kleiner.de@gmail.com    mk

  PLATFORMS: All

  PROJECTS: All

  HISTORY:
  07/16/02  awi         Pulled out of PsychPlatform.h
  11/15/02  awi         Added includes for OSX.

  DESCRIPTION:

  PsychIncludes.h includes all C, system, and language binding
  header files which a Psychtoolbox library would require.

  This file should ONLY be included by PsychConstants.h

*/

#ifndef PSYCH_IS_INCLUDED_PsychIncludes
#define PSYCH_IS_INCLUDED_PsychIncludes

#include "PsychPlatform.h"

// Platform independent include for glew: This is a catch-all
// for all OpenGL definitions and functions, currently up to
// OpenGL 4.5 + latest extensions beyond that:
#if defined(PTBMODULE_Screen) || defined(PTBMODULE_FontInfo)
#include "../Screen/GL/glew.h"
#endif

#if PSYCH_SYSTEM == PSYCH_WINDOWS
    // Need to define #define _WIN32_WINNT as >= 0x0400 so we can use TryEnterCriticalSection() call for PsychTryLockMutex() implementation.
    // For Windows Vista+ only features like GetPhysicalCursorPos() we need >= 0x600.
    // We set WINVER and _WIN32_WINNT to 0x0600, which requires Windows Vista or later as target system:
    // Ok, actually we don't. When building on a modern build system like the new Win-7 build
    // system the MSVC compiler / platform SDK should already define WINVER et al. to 0x0600 or later,
    // e.g., to Win-7 on the Win-7 system.
    // That means we'd only need these defines on pre-Win7 build systems, which we no longer
    // support. We now just have to be careful to not use post-Win7 functionality.
    // We comment these defines out and trust the platform SDK / compiler,
    // but leave them here for quick WinXP backwards compatibility testing.
    #if 0
        #define _WIN32_WINNT 0x0600
        #define WINVER       0x0600
    #endif // Conditional enable.

    // Master include for windows header file:
    #include <windows.h>

    // For building Screen, include wglew.h - Windows specific GLEW header files:
    #if defined(PTBMODULE_Screen)
    #include "../Screen/GL/wglew.h"
    #endif
// C standard library headers:
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <float.h>

//end include once
#endif
#endif