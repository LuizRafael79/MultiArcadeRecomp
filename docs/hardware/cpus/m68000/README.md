# Shared 68000 backend

This directory contains MameRecomp's shared 68000 execution runtime. The
register/flag state and exception helper implementation live in
`runtime/cpu_runtime.c`; board runtimes register their word-bus callbacks for
exception stack and vector accesses and own event/interrupt scheduling.

The active static recompilation frontend is built by the CMake target
`m68krecomp_frontend` from the upstream snapshot in
`core/third_party/m68krecomp/`. Keep imported source and its
provenance there; this directory owns MameRecomp's integration and shared CPU
runtime. The retired local partial decoder is not part of the active backend.

Musashi remains an interpreter fallback for unsupported instructions and
runtime-discovered code. Do not remove it until the recompiler covers those
execution paths and the board bus side effects remain equivalent.
