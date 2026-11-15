# Neo Geo 68000 AoT comparison

## Current pipeline

KOF98 and the Sega 68000 boards compile the same local frontend sources:
`core/third_party/m68krecomp/{decode.c,emit.c,recomp.c}`. CMake builds these
into `multiarcaderecomp_m68k_aot_frontend` and the `m68krecomp` executable. The KOF98
generator calls `rc_raw_main()` from that frontend after decrypting and
preparing its program image (`src/neogeo/kof98_aot_main.cpp`). The generated
KOF98 code and the Sega board modules do not share a board runtime: Neo Geo
uses `core/third_party/neogeo-runtime`, while System16/18 use the shared MultiArcadeRecomp
68000 CPU runtime with their own board bus and scheduling.

## Reference snapshot

The comparison target is the `neogeorecomp` submodule revision pinned by
KOF98Recomp: `a47ee3fe09dc46d1f2b86c36ec972b43b35ebf62` from
`danilopaulinodasilva/neogeorecomp`. This is distinct from the sibling local
`sp00nznet/neogeorecomp` checkout at `af96b2b` and from MultiArcadeRecomp's adapted
frontend/runtime. Keep this provenance distinction when comparing behavior.

## Semantic differences found

| File | KOF98Recomp snapshot | MultiArcadeRecomp local state | Assessment |
|---|---|---|---|
| `tools/m68krecomp/recomp.c` | Tracks bank context for the `$200000-$2fffff` program window, emits bank-tagged entries, and recognizes configured live immediate words. | The current Neo Geo raw-image path splits the region into 1 MiB physical banks, analyzes each in its own bank context, and emits bank-tagged entries. It does not recognize configured live immediate words. | The previously reported missing bank identity is stale: both frontend and runtime now preserve physical bank identity. Live-immediate support remains absent; the current MultiArcadeRecomp KOF98 profile does not apply the reference's widescreen live-word patches, so this is a feature gap rather than the cause of the verified vanilla run. |
| `tools/m68krecomp/emit.c` | Emits dynamic reads for configured live immediates and checks event deadlines on taken internal back-edges. | Emits constants for immediate operands, adds `NG_DEBUG_INSN()` to every instruction, and charges variable MULU/MULS cycles. The Neo Geo profile does not enable per-instruction event yields; its taken back-edges use the same deadline check. `single_step` is currently used by a different board profile. | The per-instruction event-yield warning in the previous version of this document was inaccurate for Neo Geo and is withdrawn. The debug hook is a measured code-size/runtime-hook cost candidate; MULU/MULS cycle charging is a timing difference to validate separately. |
| `tools/m68krecomp/decode.c` | Retains the extension-word address for immediate operands so the emitter can identify live immediates. | Does not retain that address for `EA_IMM`. | Needed before supporting live-immediate ROM patches; not implicated by the current unpatched KOF98 run. |
| `src/exec.c` | Maintains a dispatch table per physical program bank and resolves tagged entries using the currently selected bank. | Allocates one `$200000` dispatch table per bank; each lookup selects `bus_bank()`, and bank-tagged generated entries are installed by their physical bank. | The prior claim of a single dispatch table was stale and is withdrawn. Current KOF98 generation/runtime preserve bank identity for up to eight banks. |

## Verification note

An earlier standalone report identified a mismatch at `$0034f0`; that block's
`CLR.L (A0)+` / `DBRA D0,$34ea` sequence contains no immediate operand. Later
long-run mismatches were traced to verification opcode fetches seeing
`0xFFFF` through KOF98's page-zero protection overlay. The local verifier now
fetches instruction words from the underlying P-ROM during replay, while data
reads and writes still replay the recorded board bus. A subsequent
3,600-frame run checked 1,547,511 blocks with zero state/bus mismatches, and a
user run reported 336,943 blocks with zero mismatches. The old `$0034f0`
capture was not independently reproduced after this fix; do not claim a
separate root cause for that historical capture without reproducing it.

The KOF98Recomp verification claim is useful evidence for its exact frontend,
runtime, ROM preparation, and test sequence, but it does not alone establish
that MultiArcadeRecomp has the same coverage or equivalent test conditions.

## Next audit steps

1. Benchmark removal/gating of `NG_DEBUG_INSN()` for Neo Geo and measure both
   source size, module build time and runtime while preserving the hook on
   boards that use the debugger.
2. Implement bank-aware live-immediate metadata if the local KOF98 profile
   needs runtime-patched words such as the reference widescreen patches.
3. Compare timing policy and MULU/MULS cycle accounting against board traces;
   the zero-mismatch verifier compares architectural state, not elapsed cycles.
4. Keep the existing GPL-derived legacy AoT out of any replacement decision
   until the local frontend passes the required correctness and performance
   gates; retain its current fallback meanwhile.
