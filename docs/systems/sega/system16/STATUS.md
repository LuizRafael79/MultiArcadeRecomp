# Sega System 16 status

> **Frozen status (2026-11-12):** The user confirms that all currently valid Sega games were tested on 2026-11-11. The three board runtimes and nine configured game modules build; local bounded smokes also pass. `goldnaxejd` still has the previously reported orange screen and remains an explicitly deferred per-game TODO. Moonwalker's reported gameplay/audio/video failures were fixed and confirmed by the user. No further System16/18 changes are in scope while Neo Geo work begins.

## Current ownership

ROM manifests in `src/romsets/sega/system16/` are the source of truth for the
supported System16 sets; System18 sets are under `src/romsets/sega/system18/`.
CMake generates the runtime registry from TOML at build-time. Handwritten game
ROM tables in C++ have been removed.

System16A, System16B and System18 each have an independent board library under
`src/boards/sega/` and a loadable runtime module under
`src/runtime/sega/{system16a,system16b,system18}/`. Each runtime exports a
versioned C factory and is loaded by the generic Sega runner only after the
selected board has been resolved. The launcher loads the selected runtime
dynamically instead of statically linking the board runtimes. Build one runtime
with its CMake target, for example `mamerecomp_sega_runtime_system16b`.

System16A and System16B also have separate OpenGL backend sources and targets:
`system16a_video_gl.cpp` and `system16b_video_gl.cpp`. Their renderer
implementations no longer share a conditional source file. The generic Sega
runner remains in `src/runtime/sega/runner/`; board behavior is supplied
through `BoardRuntimeHardware` callbacks and the runtime module ABI.

## CPU execution

The main CPU uses the shared generated 68000 ABI and the common
`core/cpus/m68000/runtime/` bridge and Musashi fallback. System 16 bus callbacks
and FD1094 behavior stay in the System 16 integration. The common bridge no
longer has a `mamerecomp_system16_runtime` target; unrelated 68000 boards use
the generic M68000 targets.

System 16A/B sound adapters use the shared Z80 backend. AOT coverage is
configured per game; the common interpreter remains available when code is not
translated.

Each sound adapter owns its board's CPU/device timing, routing and mix, then
delivers PCM through the generic `SegaAudioSink`. The SDL playback stream lives
in the Sega presentation module, so board sound implementations do not open a
host audio device.

## FD1094 and ESWAT

The ESWAT profile currently enabled for the runner is `eswat`, MAME's set 4
(World), protected by FD1094 key `317-0130`. MAME's `segas16b.cpp` assigns it
the `system16b_fd1094_5797` machine configuration and loads the key in the
`maincpu:key` region. The project loads that key and applies the MAME-derived
FD1094 opcode cipher and opcode mask table. The Musashi bridge decrypts opcode
fetches while leaving data reads alone, handles encrypted reset vectors, and
tracks state changes from `CMPI.L`, IRQ entry, and `RTE`. The user confirmed
that this protected set runs correctly.

Protection support is not AoT support. The Sega `build --game=eswat` path checks
the program, key, graphics, and sound ROMs, then builds the host only. It skips
the game's translated module; the whole 68000 runs through Musashi fallback.
Generating state-aware FD1094 AoT remains open. Other original FD1094 sets,
including ESWAT sets 1-3 and the original encrypted Golden Axe sets, have TOML
ROM manifests but are not enabled as runtime profiles yet; enable and validate
them one at a time.

## Remaining work

- Continue per-game runtime checks for boot, video, sound, inputs, and long
  runs. Passing a build or frame-count run alone does not establish a functional
  game; the known per-game gaps are tracked in `docs/TODO.md` and the System 18
  TODO.
- Keep game-specific code optional: ROM manifests without verified special
  behavior should not require a game C++ module.

The current software/OpenGL video and game-specific checkpoints are recorded in
[`video status`](../../../hardware/video/STATUS.md) and
[`Golden Axe`](../../../games/sega/golden_axe/STATUS.md).


## I8751 protection MCU

O core MCS-51 genérico fica em `core/cpus/mcs51/`, licenciado conforme
`docs/license/mame-mcs51/BSD-3-Clause`; o chip Sega I8751 está em
`core/chips/sega_i8751/`. Ele é independente do FD1094:
o firmware executa pelo interpretador MCS-51, com XDATA/portas conectados ao
mapper 315-5195 em System16B e System18, ou aos bancos e sinais System16A.
O mapper encaminha IRQs do MCU, IRQs ao 68000 e a linha de reset do 68000.

O perfil `goldnaxe` corresponde ao original com I8751 317-0123A e configuração
`system16b_i8751_5797`. `goldnaxe1`, `goldnaxe3`, `goldnaxej` e `goldnaxeu`
são os originais FD1094 e continuam usando o módulo FD1094 existente; os
bootlegs `goldnaxeud`/`goldnaxe1d` não usam essas proteções. O core MCS-51
ainda não tem gerador AOT; a cobertura AOT permanece pendente no TODO global.


### Diagnóstico em andamento: `goldnaxe` I8751

A captura preta de 50 milhões de ciclos e o PC inválido registrados antes são
diagnósticos de uma build anterior. Na execução atual de 300 vblanks, o set
terminou em PC `0x5b1a`, o 68000 executou 210.629 instruções e o mapper manteve
reset liberado; isso supera a falha imediata, mas ainda não comprova entrada em
gameplay nem paridade com MAME. Falta conferir visualmente o boot e validar a
resposta de proteção/inputs do I8751 em uma sessão jogável.
