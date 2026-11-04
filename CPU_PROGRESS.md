# 68000 implementation log

This file is the durable checkpoint for the CPU work. Update it whenever a meaningful opcode, bus behavior, or execution milestone lands. The chat is not the source of truth.

## Goal and constraints

- Recompile the Shinobi set 6 main 68000 program into native code while modeling the System 16A board.
- No HLE, patched control flow, invented return values, or substituting game routines with host-side approximations.
- Treat the local MAME source as a hardware/ROM reference, not a runtime dependency.
- Keep these milestones distinct: (1) decoder coverage, (2) valid generated C, (3) correct CPU semantics, (4) integrated guest execution, (5) correct devices and game behavior. Passing an earlier milestone does not establish a later one.

## Current checkpoint — 2026-10-29

### Current status

- The NeoGeoRecomp 68000 frontend is linked into MameRecomp. `translate-full` produces ROM-derived C/table output, and an optional CMake build compiles that output into the executable.
- `run-full` initializes reset state, dispatches generated code through the System 16A bus, and schedules 60 Hz vblank IRQ4 using 10 MHz CPU cycles. Runs at 50M, 140M, and 300M cycles execute generated code without a bus fault and produce changing guest video state.
- A controlled Coin/Start/P1 sequence advances Shinobi into a gameplay scene. At 150M guest cycles, the generated 68000 path, System 16A bus, and software video capture match MAME AVI frame 898 pixel-for-pixel. This is evidence for that tested path and state; it does not establish full 68000 semantic correctness or general playability.
- Shinobi System 16A graphics ROMs load with CRC validation, reconstruct tile/sprite regions, and decode 8,192 tiles. The software System 16A compositor draws page layers, text, sprite descriptors, and priority/shadow effects into PPM captures, with exact RGB parity at multiple title, attract, and gameplay states. Shadow, flip, row/column scroll, raster timing, and other priority combinations remain unverified; see `VIDEO_PROGRESS.md` for exact captures and current diagnostics.
- Next CPU integration work is to validate register/interrupt behavior against MAME while the video model is refined. Keep CPU correctness, board-device accuracy, and visible game behavior as separate claims.

### Retired prototype: original partial translator

The local decoder/emitter under `core/cpus/m68000/m68k.cpp` and its `translate`
CLI route were removed when the project standardized on the shared
`m68krecomp_frontend`. The following numbers and commands are historical only;
they cannot be reproduced from the current tree and do not describe the active
68000 backend.

With the local `shinobi.zip` and assembled main CPU ROM at `build/shinobi_maincpu.bin`, the static translator reported:

```text
Translated 656 instructions in 227 blocks; 40 block(s) stop at unsupported code.
```

The generated file compiled as a strict C object (`cc -std=c11 -Wall -Wextra -Werror -c`). The CMake target also built successfully. These checks validate the translator build and generated-C syntax only. **The translated blocks are not being executed; Shinobi does not run yet.** The block walk follows statically discovered branch/call edges and therefore includes alternative paths that may not execute on a particular boot.

Reproduce from the repository root (adjust ROM paths for the local checkout):

```sh
cmake --build /tmp/mamerecomp-build -j2
/tmp/mamerecomp-build/mamerecomp translate shinobi /home/luiz/git/mame/roms/shinobi.zip \
  --c-out /tmp/shinobi_translated.c > /tmp/shinobi-translate.log 2>&1
cc -std=c11 -Wall -Wextra -Werror -c /tmp/shinobi_translated.c -o /tmp/shinobi_translated.o
```

### Recent reset-path work

Opcode forms added while following Shinobi's early initialization and its NVRAM/board setup include:

- `CMP.W d16(An),Dn`, with subtraction flags and memory-bus read.
- `MOVE.L (An)+,(Am)+`, with big-endian word accesses and both address updates.
- `MOVEA.L #imm,An` and `MOVEA.W #imm,An` (word form sign extends; neither changes condition codes).
- `MOVE.L Dn,(An)`, `MOVE.L Dn,d16(An)`, `MOVE.W #imm,(An)`, and `MOVE.L #imm,Dn`.
- Earlier additions cover indexed byte/word reads and writes, quick byte updates, byte/word compares, word shifts, and video-table setup operations. Definitions and emitters are in `core/cpus/m68000/m68k.cpp`; the enum and instruction fields are in `core/cpus/m68000/m68k.hpp`.

### Runtime fallback and executable RAM

Musashi remains part of the hybrid execution path while the native translator
does not cover every instruction. Keep its fallback visible in coverage and
traces; do not report fallback-executed instructions as native coverage.

ROM-only discovery cannot cover code copied, decrypted, or patched in RAM. The
next architecture step is dynamic discovery when the guest PC enters executable
RAM, with translated blocks keyed by address and the current RAM contents (or a
write generation for the affected range). Writes overlapping translated code
must invalidate those blocks. Until then, unsupported or newly observed RAM
code must continue through Musashi, preserving the same bus and device side
effects as native code. This is a shared 68000 backend concern; boards provide
their executable-memory map and invalidation notifications rather than growing
board-specific recompilers.

The retired local static walk reached NVRAM checksum comparison and copy/clear loops, then continued into board initialization. These addresses and coverage notes refer only to that prototype and should not be used to assess the active frontend.

### Correctness work still required

- Build instruction-level checks for flags, partial-register writes, address-register side effects, indexed effective addresses, and postincrement ordering. Generated-C compilation alone is not a semantic test.
- Audit 68000 bus-cycle details for memory read-modify-write instructions (including `CLR`), odd-address faults, exceptions/interrupts, supervisor state, and prefetch behavior where the game depends on it.
- Keep bus reads/writes observable by devices; do not replace device polling with constants to make startup proceed.
- Implement an execution loop that dispatches translated blocks while preserving guest PC/register/SR state, then compare traces against the MAME CPU reference for selected boot scenarios.
- Continue the device map from the System 16A driver and device implementations; current I/O/PPI behavior is partial. A static block graph cannot establish device correctness.

## Historical next action

The original partial-translator milestone suggested inspecting opcode `0x1451c`. That task was superseded when the project integrated the complete NeoGeoRecomp frontend; current video work is tracked in `VIDEO_PROGRESS.md`.

## Audio reminder

The System 16A sound path is not “run Z80 and emit a file.” It includes the Z80 program, its memory map and timing, plus the board's sound chips and register behavior. Audio work comes after the CPU/video foundation; no MP3 substitution or HLE shortcut is planned.

## External 68000 recompiler reference — 2026-10-29

The local NeoGeoRecomp clone is at `/home/luiz/git/neogeorecomp` and its submodules are initialized. Its raw-image `m68krecomp` was built out-of-tree under `/tmp/neogeorecomp-build` and run on `build/shinobi_maincpu.bin` with base 0 and reset entry `0x400`. It found 1,510 pointer-scan seeds and emitted 117,159 instruction occurrences across 1,811 routines and 3,833 dispatch entries. Output was about 17 MB. This demonstrates broad static discovery and full 68000 decode/emission are available, but it is not a like-for-like comparison with our current 748 decoded instructions / 246 basic blocks / 35 unsupported blocks: their routine output duplicates shared instructions and includes speculative pointer candidates. The output was not syntax-compiled or executed, so this is a coverage experiment, not CPU correctness evidence.

A snapshot of the NeoGeoRecomp `tools/m68krecomp` sources is staged under `core/third_party/neogeorecomp-m68krecomp/` with its MIT license and provenance. Its emitter expects the Neo Geo `CPU` and `ng_*` runtime ABI. Integration work must adapt that ABI to the System 16 address-space callbacks, timing/event scheduler, interrupts/exceptions, and board devices; Neo Geo bus/video/audio code is not applicable. Keep ROM-derived generated C out of version control. The upstream `--verify` design and Musashi fallback are useful correctness-floor references; final native coverage must remain measurable separately from interpreted fallback execution.

### NeoGeoRecomp frontend staged in the build

The MIT-licensed upstream `decode.c` and `emit.c` are compiled as the shared `m68krecomp_frontend` library. All active 68000 translation entry points should use this frontend; generated Neo Geo-style C is adapted to each board's bus/runtime without importing Neo Geo board behavior. Musashi remains the interpreter fallback for instructions not yet implemented by the recompiler, especially dynamically reached or RAM-resident code, and must remain until that gap is closed.

### Board-independent CPU runtime staged

The NeoGeoRecomp register/CCR state, ALU helpers, and ordinary exception-entry implementation are now staged and build as `m68k_cpu_runtime`. The Neo Geo page-map/bus implementation is not linked. A System 16 adapter still needs to route 24-bit big-endian reads/writes through `System16AddressSpace`, preserve observable MMIO accesses, report unmapped/bus faults loudly, and schedule 10 MHz CPU cycles against video events. System 16A's vblank IRQ4 must be delivered at the board event boundary. The imported generated code still is not dispatched by the executable.

## Sound CPU and board audio reference — 2026-10-29

The MIT `superzazu/z80` core from the NeoGeoRecomp submodule is staged at `core/third_party/superzazu-z80/` and compiles as the separate `z80_core` target. It is an instruction-level interpreter (`z80_step`), with memory/port callbacks and a T-state counter; it is not a Z80 recompiler. Upstream documents passing ZEXDOC/ZEXALL, but this project has not run those suites. The emulator executable does not use the Z80 target yet.

The local MAME System 16A Shinobi config is the hardware source: Z80 at 4 MHz; program map `$0000-$7fff` ROM, `$e800` sound-latch read, `$f800-$ffff` RAM; I/O ports `$00/$01` YM2151, `$80` uPD7751 command, and `$c0` sound-latch read. The latch read asserts the 8255 PC6 acknowledge. YM2151 is clocked at 4 MHz and its output port controls uPD7751 reset/IRQ. The uPD7751 is clocked at 6 MHz, reads sample data through its bus input, uses an i8243 expander for ROM address selection, and drives the board's 8-bit R2R DAC. Thus the Z80 core is reusable, and ymfm's YM2151/OPM core is a candidate; the uPD7751 + i8243 + DAC path still needs its own faithful implementation or a compatible existing device core.

### System 16 bus ABI bridge

Added `src/system16/system16_m68k_bus.{h,c,cpp}`. It binds the generated-code bus ABI to `System16AddressSpace` read/write callbacks, masks addresses to the 24-bit 68000 bus, keeps page-map entries null so ROM/RAM/MMIO all pass through the board map, and latches the first failed access (width, direction, address) instead of silently treating it as valid memory. The CPU runtime and bridge compile and link into the executable. No generated block or CPU dispatcher calls the bridge yet; the runtime must stop/raise the appropriate fault after an access failure before this is execution-ready.

### Sound CPU scope decision

Use the NeoGeoRecomp Z80 interpreter first. Its `z80_step` model advances one instruction and tracks T-states; implement the System 16 sound map/ports and synchronize it with the board scheduler. Static Z80 recompilation is deferred unless profiling later shows a real need. The Z80 target currently compiles but is not linked into the game runtime.

### Full 68000 output compiled as native C objects — 2026-10-29

The `translate-full` path was run on the local System 16A Shinobi ROM set. The NeoGeoRecomp frontend discovered 1,510 pointer-scan seeds, 117,159 instruction occurrences, 1,811 routines, and 3,833 dispatch entries. Its output was then compiled with the system C compiler into two native objects: `shinobi_recomp_00.o` (~8.4 MB) and `shinobi_recomp_table.o` (~224 KB). Strict C syntax checking also passed with `-std=c11 -Wall -Wextra -Werror`.

This answers the portability question: the shared 68000 frontend can translate a System 16 program image into C on Linux. This does **not** yet mean the game runs, or even that the generated objects link into the application. The generated objects still require the Neo CPU ABI (`ng_cpu`, `ng_cycles`, `ng_next_event`, flag/ALU helpers), the memory slow-path (`ng_r8/16_slow`, `ng_w8/16_slow`, plus null page maps), and the System 16 dispatcher for `ng_recomp_table` / `ng_recomp_count`. The CPU state, memory bridge, and reusable dispatcher are staged, but the application does not yet link ROM-generated objects or dispatch them. Keep generated C/object files and ROM-derived data out of version control.

The immediate implementation milestone is a bounded CPU execution harness that initializes from the reset vectors, dispatches generated entries repeatedly, sets an event budget, and reports bus faults. That will validate real guest instruction execution before adding video/audio scheduling and interrupt timing; it is not a claim that Shinobi boots yet.

### First dispatcher ABI milestone — 2026-10-29

Added `src/system16/m68k_recomp_dispatch.{h,c}`. It binary-searches the address-sorted `ng_func_entry_t` table emitted by the frontend, invokes one generated routine for `ng_cpu.pc`, and reports a missing entry, invalid odd PC, generated bad-dispatch, or latched System 16 bus fault. The generated `ng_bad_dispatch` callback now has a concrete implementation. The dispatcher clears the bus-fault latch before each invocation so each result describes that invocation.

CMake rebuilt `mamerecomp` and `m68krecomp` successfully after this addition. This validates compilation/linking of the reusable dispatcher with the CPU state and bus ABI; the ROM-generated object is still an out-of-tree artifact and is not linked into `mamerecomp`, and the dispatcher has not yet been run against the Shinobi ROM. A caller still needs to initialize the reset state, set `ng_next_event`, dispatch repeatedly, schedule System 16 events/IRQs, and handle a bus fault with correct 68000 semantics. At this stage a bus fault is reported to the caller; the dispatcher does not invent an exception frame or continue execution.

### CPU-only generated-code execution — 2026-10-29

`translate-full` now writes a tiny table adapter in its output directory. CMake accepts `-DMAMERECOMP_GENERATED_DIR=<one game output dir>` and compiles that local generated C into `mamerecomp`; generated ROM-derived files remain outside the source tree. Added `run-full <profile> <romset.zip> [--cycles N]`, which initializes supervisor state, SP and PC from the reset vectors, binds the System 16A address space, and repeatedly dispatches native routines up to an event/cycle budget. Cross-checked the reset stack against MAME's `segas16a_map`: high addresses alias the mirrored NVRAM, so the initial SP `0xFFFFFF00` resolves to `0xC73F00`; no extra high work-RAM region should shadow that mirror.

On the local Shinobi set 6, the generated-code build linked successfully and CPU-only runs completed with no unmapped bus access through 10 million 68000 cycles. At 1 million cycles it reported 88,258 instructions, 52 dispatches, PC `0x3526`, SP `0xFFFFFEFC`; at 10 million cycles it reported 906,440 instructions and the same PC/SP. `0x3526` is the guest's polling/delay routine, so it is still in initialization. This is first evidence that generated guest instructions execute through the System 16 memory bridge; it is not a boot or gameplay milestone. The CPU harness now schedules a vblank boundary from MAME System 16A timing (10 MHz / 60 Hz) and requests IRQ4; it still has no video/audio or scanline/device scheduler. The dispatcher accepts the autovector, honors the CPU interrupt mask, wakes STOP, pushes the normal 68000 frame through the System 16 bus, and charges 44 cycles according to the Musashi exception table. The PC remaining in this delay helper across budgets means the next step is to log its data registers and trace its callers, then determine whether it is expected initialization delay or waits for a board signal. Do not force a return value; add only the event/device behavior that the MAME System 16A reference confirms.


### System 16A vblank IRQ4 path — 2026-10-29

Shinobi reset code calls the delay helper at `0x3522`; its loop polls guest byte `$F01C`. The translated IRQ handler at `0x2878-0x2880` observes and increments that same byte. This establishes that the CPU-only runner was waiting for an interrupt, not for a missing write from a video register. MAME's `segas16a_state::system16a` config specifies a 10 MHz 68000, 60 Hz screen, and `irq4_line_hold` as the vblank interrupt. Added a rational 10 MHz/60 Hz event boundary and level-4 autovector acceptance in the runner, without fabricating guest memory values. The shared dispatcher now accepts vectors 25-31, honors `ng_cpu.ipl`, wakes STOP, uses the generic 68000 exception frame/vector path, and charges 44 cycles for the autovector as specified by the local Musashi table.


With IRQ4 scheduling enabled, the same 1,000,000-cycle run now advances from the reset delay to PC `0x3188`; it completes 88,243 guest instructions and accepts vblank interrupts. At 10,000,000 cycles it executes 924,532 guest instructions and repeatedly reaches the reset delay helper between vblank-driven work; no bus fault is reported. This demonstrates IRQ acceptance and execution of the game’s interrupt handler sufficiently to change the guest wait state, but does not establish that the startup sequence or screen is correct. The runner reports accepted IRQ4 count and full D/A registers for further tracing. Vblank cycle placement uses a remainder accumulator (`10,000,000 / 60`) to avoid long-run drift.


At 50,000,000 guest cycles, the IRQ-enabled CPU harness completes 4,641,264 instructions, accepts 297 IRQ4 events, and still reports no bus fault. It ends at `0x3526`, the same vblank wait helper, with the same stack location; this is consistent with a recurring frame wait, not evidence by itself of a deadlock. Rendering and game-state validation are still absent, so this milestone only confirms the CPU loop plus periodic IRQ path.

The host 68000 frontend/runtime now also advances through periodic System 16A vblank, with the guest ISR executing and changing RAM state. It remains CPU-only; see `VIDEO_PROGRESS.md` for the next video pipeline milestone.


### Instrução como fronteira de evento — 2026-10-29

O emissor 68000 compartilhado agora verifica `ng_next_event` depois de cada instrução concluída e antes de seguir um desvio interno. Para permitir retomar no PC de uma instrução intermediária, o frontend atribui cada endereço de instrução traduzida a uma rotina dispatchável; o Shinobi gerou 18.252 entradas para 117.368 ocorrências de instrução em 1.821 rotinas. A primeira versão que retornava PCs internos falhou em `0x318a` porque a tabela só continha entradas de retomada antigas; o mapeamento completo resolveu esse caso sem criar uma função por opcode.

O build regenerado passou e execuções até 165M ciclos completaram sem PC sem entrada nem falha de barramento. Os snapshots por vblank seguem reproduzíveis: vblank 990 mantém correspondência exata com MAME; vblank 987 ainda difere por 444 pixels. Uma execução sem controles até 800M ciclos revelou PC indireto `0x149a8`; essa entrada foi acrescentada à geração, e o novo build completa 4.800 vblanks sem PC sem rotina. Assim, a fronteira de evento está agora implementada, mas os desvios que restam são mudanças observáveis de estado do jogo/sprites, não evidência de que o renderer ou a nova tabela estejam errados. A execução continua atômica por instrução: um opcode que cruza o evento termina antes de o escalonador capturar o estado.

## CLI debugger — 2026-10-29

Added an opt-in instruction-level monitor to the generated 68000 path. `run-full --debugger` supports an initial stop, `c`/`s`, PC breakpoints, register/SR/cycle/vblank output, safe ROM/RAM peeks, and write watchpoints. The emitter tags each generated instruction with its guest address; its hook pointer remains null unless the monitor is enabled. The System 16 bus notifies the monitor only after successful writes, allowing the next instruction hook to stop at a stable post-write boundary. Build outputs generated before this emitter change must be regenerated for instruction hooks to be present.

### Operand-dependent multiply timing — 2026-10-29

The generated path previously charged the fixed 38-cycle table entry for every `MULU`/`MULS`. MAME's 68000 implementation performs the multiply as a bit-serial microstate sequence, so those operations consume operand-dependent time. The emitter now adds 2 cycles per set multiplier bit for `MULU`, and 2 cycles per bit transition (including the initial zero-to-bit transition) for `MULS`, matching the work in the corresponding MAME microstate loops while leaving register/flag results unchanged. The helper lives in the shared NeoGeoRecomp-derived CPU ABI, so other boards using this translator receive the same CPU behavior.

Rebuilt the frontend and regenerated Shinobi (1,821 routines; 117,368 translated instruction occurrences). A controlled rerun to physical vblank 987 completed with no bus fault. Its vblank framebuffer and sprite RAM are byte-identical to the prior MameRecomp capture, so this change did not hide or move the known sprite residual; the capture still needs comparison against MAME after tracing the upstream game-state calculation. This is one identified timing correction, not proof that every instruction's timing or the whole core is equivalent to MAME. Next audit variable-time `DIVU/DIVS` and compare CPU-visible state at exact board events; treat MAME as a behavioral reference rather than a mandate to share its internal core implementation.

Follow-up trace around the sprite source-table write (`0xFFF840`) shows the relevant `D0/D1` values are already `0xC9/0x100A6` at PC `0x38E4`, before the output routine at `0x396C`; MAME's corresponding write at 16.4183509 s carries `0xC8/0x100A5`. The fixed-multiply timing change shifts this source write by two cycles in the local trace, but the operands and vblank-987 frame remain unchanged. The observed sprite delta therefore is not explained by this `MULU` path. Trace backward through the game logic that updates the source object's coordinates and compare there before drawing a conclusion about the CPU core.

MAME's local `docs/source/techspecs/cpu_device.rst` defines interruptibility as suspending an instruction and resuming it later, including aborting and reissuing a memory access. The MAME 68000 implementation has `m_inst_substate` and checks the remaining cycle budget after individual interruptible bus accesses. MameRecomp currently increments the whole opcode's cycle count at instruction entry (`NG_INSN`) and only checks the next board event after the translated instruction finishes; its bus callbacks do not carry a guest-cycle timestamp. Therefore it cannot currently reproduce MAME's mid-instruction access ordering, even if the final arithmetic result agrees. This is a known execution-model difference, not evidence by itself of an incorrect opcode implementation. A finer model would need explicit cycle points for guest bus accesses plus resumable generated-instruction state, or a bounded board-level compromise validated against exact RAM/video event captures. Do not attempt to copy MAME's microstate machinery without first showing that this finer granularity is needed for the games and boards targeted here.
# Trace de leituras do barramento — 2026-10-29

O trace opcional `--cpu-trace` agora inclui eventos `R` para leituras de 8 e 16 bits, com ciclo, PC da instrução, endereço e valor, além dos registros de instrução (`I`) e escritas (`W`). Isso permite observar o dado realmente consumido por instruções de memória sem registrar a execução inteira nem depender de inferências pelo valor escrito. A captura é limitada ao intervalo de ciclos configurado e só é habilitada junto do trace/debugger.

Ao repetir a divergência de `ffc012` em execuções limpas, sem importar arquivo NVRAM pré-existente, o primeiro boot do MameRecomp grava `0x8000` nesse word já no attract loop (por volta do vblank 502), alternando entre `0x0000` e `0x8000` nos frames seguintes. Em torno de 16,4013 s, o `ADD.L` em `PC=0x39d4` recebe `D0=0x20000`: MameRecomp lê `ffc010:ffc012=05e5:8000` e grava `05e7:8000`, enquanto MAME, também sem NVRAM anterior, lê `05e5:0000` e grava `05e7:0000`. A NVRAM é estado criado/atualizado durante a execução e persistido depois; não é um artefato compilado nem uma entrada obrigatória para o primeiro boot.

O trace posterior conecta esse estado ao residual de vídeo sem implicar erro no renderer. Em torno de 16,417963 s, ambos chegam ao mesmo `ADD.L` com `D0=0x28000` e `ffc010=0x05e7`; o recompilado lê `ffc012=0x8000` e grava `ffc010:ffc012=05ea:0000`, enquanto MAME lê `ffc012=0` e grava `05e9:8000`. Na rotina de coordenada seguinte, o recompilado lê `ffc010=05ea`, e MAME lê `05e9`; após as subtrações em `0x386c`/`0x3870`, isso produz `D0=0xc9` contra `0xc8`, que alimenta os limites de sprite (`c9a6` contra `c8a5`). O cálculo aritmético observado é consistente com os respectivos operandos. Portanto, o residual localizado de um pixel já está no estado de origem do sprite. A causa anterior da diferença de `ffc012` continua em aberto; ainda não se atribui isso a um opcode do recompilador, ao Musashi ou a uma exigência de timing ciclo a ciclo do MAME.

O trace identifica ciclo de instrução, não o timestamp de cada ciclo de barramento. Não usar esse resultado para exigir igualdade interna com o MAME ou justificar um ajuste de temporização sem mais evidência.

Uma captura limpa mais antiga mostra por que é preciso comparar a trajetória antes de igualar palavras de NVRAM por tempo guest: o primeiro `PC=0x39d4` registrado no MAME aparece no screen frame 501 em `t=8.3446165 s`; no MameRecomp aparece em `cycle=83,779,446` (`t≈8.377945 s`), cerca de dois frames depois. Nesse primeiro acesso observado, ambos têm `D0=0x8000`, leem `ffc010=0x05f0` e `ffc012=0`, e produzem `05f0:8000`; portanto a operação em si coincide. No instante do acesso MAME, o recompilado ainda estava na rotina de espera `0x3188/0x318a`, preenchendo RAM. Isso é evidência de fase/fluxo de execução diferente nessa parte do boot, não de erro aritmético no `ADD.L`. Ainda precisamos capturar a sequência completa de chamadas e seu estado para localizar quando o fluxo passa a divergir.

Na divergência visual reproduzível do vblank 987, os valores já chegam diferentes à rotina de preparação do sprite: na gravação de `fff840` em `0x396c`, o recompilado parte de `D0=0xc9`, `D1=0x000100a6`; o tap do MAME registra `D0=0xc8`, `D1=0x000100a5` ao escrever o valor correspondente. A rotina de cópia posterior (`0x28f0`) preserva esse passo de diferença (`c9a6` contra `c8a5`). O MAME expõe um PC adiantado pelo prefetch, então o PC reportado não foi usado como igualdade de fronteira. Essa evidência localiza a divergência no estado guest antes da composição, mas ainda não determina se ela vem da instrução anterior, de uma diferença de fase/evento ou de estado de gameplay acumulado.

Importante para atribuir a causa: `run-full` executa as rotinas C nativas geradas pelo frontend NeoGeoRecomp. O Musashi está ligado como referência/fonte de tabelas e apoio ao frontend, mas não interpreta continuamente esse trecho durante o jogo. Portanto, a divergência observada não demonstra por si só um defeito do Musashi. O MAME usa outro core 68000; diferenças podem vir da semântica emitida pelo recompilador, dos helpers compartilhados derivados do runtime NeoGeoRecomp, do agendamento em fronteiras de instrução/evento ou de estado/input. É preciso comparar a primeira instrução e os operandos que divergem antes de atribuir a um core específico.

### Reavaliação do estado `ffc012` com Free Play sem Coin — 2026-10-29

Os traces acima foram produzidos numa sequência que enviava Coin 1 com o DIP de Free Play, como nas capturas históricas do MAME. O teste sem Coin foi uma sequência alternativa; sua imagem idêntica em um pareamento não demonstra a causa do estado diferente nos testes com Coin. Uma repetição recente com Coin em ambos os lados ainda mostra um delta de seis bytes de texto no snapshot do vblank, então a ordem de entrada, escrita da RAM e atualização da tela continua em investigação. A diferença em `ffc012` e o passo na coordenada foram observados na trajetória com Coin, mas a causa interna não foi isolada; não atribuí-los a um opcode, Musashi ou timing geral sem traces sincronizados com entradas e estados equivalentes.
