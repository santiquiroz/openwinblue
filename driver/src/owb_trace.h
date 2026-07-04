// driver/src/owb_trace.h
//
// Trazas siempre activas del driver. KdPrint desaparece en builds release;
// DbgPrintEx en DPFLTR_ERROR_LEVEL pasa el filtro por defecto de Windows,
// así que se ve en DebugView (Capture Kernel) y WinDbg sin tocar el registro.
//
#pragma once
#include <ntddk.h>

#define OwbLog(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, \
               "OWB: " fmt "\n", ##__VA_ARGS__)

// Nombre legible del estado AVDTP para las trazas.
#define OwbAvdtpStateName(s)                                   \
    ((s) == 0 ? "Idle"        :                                \
     (s) == 1 ? "Connecting"  :                                \
     (s) == 2 ? "Discovering" :                                \
     (s) == 3 ? "Configuring" :                                \
     (s) == 4 ? "Configured"  :                                \
     (s) == 5 ? "Open"        :                                \
     (s) == 6 ? "Streaming"   :                                \
     (s) == 7 ? "Closing"     : "???")
