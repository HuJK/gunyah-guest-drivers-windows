#pragma once

#include <ntddk.h>
#include <wdm.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>
#include <ntstrsafe.h>

#include "trace.h"
#include "viosnd.h"
#include "ViosndPcm.h"
#include "ViosndVirtio.h"
#include "ViosndTopology.h"
#include "ViosndWaveRT.h"

/* Diagnostics are off upstream, which hides the one thing worth seeing on a pVM:
 * the render worker's "no completion loops=N outstanding=N" line and the
 * NotificationCount the OS handed us, which together say whether TX starvation is
 * the driver running out of packets it is allowed to send ahead. Turned on here
 * while that is under investigation -- DbgPrint needs a consumer (a kernel
 * debugger or DebugView) to be visible at all, so it costs nothing when nobody is
 * looking. Flip back to 0 before this is anything but a debugging branch. */
#ifndef VIOSND_ENABLE_LOG
#define VIOSND_ENABLE_LOG 1
#endif

#if VIOSND_ENABLE_LOG
#define VIOSND_LOG(...) DbgPrintEx(__VA_ARGS__)
#else
#define VIOSND_LOG(...) ((void)0)
#endif

inline void *__cdecl operator new(size_t, void *Address)
{
    return Address;
}

inline void __cdecl operator delete(void *, void *)
{
}
