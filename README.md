# MameRecomp

Current subsystem boundaries and planned execution models are recorded in [ARCHITECTURE.md](docs/architecture/OVERVIEW.md); implementation checkpoints are tracked in [CPU_PROGRESS.md](docs/hardware/cpus/STATUS.md), [docs/hardware/cpus/z80/README.md](docs/hardware/cpus/z80/README.md), and [VIDEO_PROGRESS.md](docs/hardware/video/STATUS.md).

Recompilador experimental para jogos arcade das famílias **Sega System 16A/16B e System 18**, começando pelo Shinobi set 6 (sem FD1094).

A meta é compartilhar um backend 68000 e um runtime System 16 entre vários jogos. Cada jogo ainda precisa de um perfil de ROMs e algumas diferenças de placa/jogo podem exigir suporte específico. O MAME é referência de ROMs, hardware e validação; não será uma dependência de execução do jogo recompilado.

Também há uma trilha **experimental de Neo Geo**, isolada dos módulos Sega.
KOF98 já é compilado como módulo de jogo e executado pelo host `mamerecomp`
junto ao runtime Neo Geo carregável. A compilação usa o frontend 68000 local;
o jogo mantém sua transformação PROGSF1 e proteção. O Z80 ainda usa o backend
AoT legado como padrão, com o SuperZazu disponível para diagnóstico. Veja
[status e auditoria de modularização Neo Geo](docs/systems/snk/neogeo/STATUS.md)
e [comparação do AoT 68000](docs/neogeo/68000-aot-comparison.md).

O backend AoT Z80 escrito para o MameRecomp chama-se **ZUni80**. Seu código
atual tem decodificação, descoberta de blocos e agrupamento de rotinas, mas
ainda não tem emissor/runtime AoT funcional. Ele ainda não substitui o backend
legado GPL-3.0-only usado em produção; o SuperZazu MIT é apenas uma alternativa
de interpretação e não gera AoT. A migração continua pendente até o ZUni80
emitir código, preservar o contrato de CPU compartilhado e alcançar paridade
funcional e de desempenho nas placas que usam Z80.

O ponto de entrada comum para builds por placa é `tools/recompile.py`, que
recebe `--system` e `--game` e encaminha as opções ao backend correspondente.
Neo Geo/KOF98 usa o protótipo local; CPS2 já encaminha para o pipeline local do
`cps2recomp`, mantendo artefatos sob `build/cps2/`. Os demais sistemas entram
na mesma interface conforme seus adaptadores de jogo forem ligados. Exemplo:

KI1/KI2 também têm um fluxo experimental nesse dispatcher. KI1 decodifica as
imagens de programa comprimidas na U98 e gera C AOT MIPS III; o CHD é dado de
runtime, não origem de instruções. KI2 ainda usa captura de RAM pós-boot pelo
MAME até confirmarmos o formato da BIOS. Ainda não é possível executar esses
jogos pelo host, pois o runtime R4600 compartilhado não existe. Detalhes em
[Killer Instinct pipeline](docs/systems/midway/seattle/KILLER_INSTINCT_PIPELINE.md).

O trabalho CPS está documentado em [CPS_PROGRESS.md](docs/systems/capcom/cps/STATUS.md). O alvo
CPS1 `sf2ce` valida as ROMs e gera um módulo AOT por jogo,
`libmamerecomp_game_sf2ce.so`, que depende do runtime da placa
`libmamerecomp_cps1.so` e dos runtimes compartilhados 68000/Z80. O executável
`mamerecomp_sf2ce` é um dispatcher/runner pequeno que carrega esses módulos.
As ROMs continuam externas. Musashi e o interpretador Z80 seguem como
fallbacks. `dino` tem geração Z80 local com Kabuki e runtime QSound; o runner
já mostra o logo da Capcom, mas attract e gameplay ainda não foram validados.
O 68000 compartilhado e o fallback Musashi continuam ligados.

```sh
python3 tools/recompile.py --system neogeo --game kof98 \
  --rom-dir /path/to/roms --jobs 2

build/mamerecomp build --machine=cps --board=cps1 --game=sf2ce
build/mamerecomp run --machine=cps --board=cps1 --game=sf2ce
python3 tools/recompile.py --system ki --game ki1
python3 tools/recompile.py --system ki --game ki2
```

`build` validates and exports the game's ROM regions from the MAME ZIP through
its C++ ROM driver, then creates the static translation. It reuses a current
build; `--rebuild` forces a fresh one. `run` only launches an existing current
build and reports when `build` is needed. Both commands accept
`--rom-dir /path/to/sf2ce.zip` to override the default ROM location. Audio
samples stay inside the exported ROM region; no separate OKI file is needed.
When using a custom ZIP, pass the same `--rom-dir` to both commands.

## Repository layout

- `core/cpus/`: shared CPU models and helpers, grouped by CPU family (`m68000`, `mcs48`, `z80`). A board selects the CPU backend it needs and supplies its bus, clock, interrupt, and device wiring.
- `core/chips/`: shared chip models, grouped by chip number/family.
- `core/third_party/`: upstream dependency snapshots used by the core. Keep each upstream isolated; do not mix Neo Geo board code into CPS1 or Sega modules.
- `src/boards/`: board implementations grouped by manufacturer and board family; Sega System 16A, System 16B, and System 18 are separate modules.
- `src/runtime/`: execution, scheduling, and device orchestration shared at a system-family boundary.
- `src/games/`: game-owned profiles and behavior where that split has been migrated.
- `src/romsets/`: TOML ROM manifests organized by publisher/module/board/set; see [ROM-set catalog](docs/romsets/README.md).
- `tools/generators/`: source-side build pipelines and manifest/code generators, organized by board or data type. Their generated code and binaries belong under `build/`, not beside the generator sources.
- `tools/`: per-system command-line entry points, generators, and analyzers.
- `artifacts/`: captures and diagnostic logs that are not source code.
- `build/`: regenerable build outputs, translated code, and working ROM files; ignored by Git.

As notas de proveniência estão em `docs/third_party/` e os textos de licença
e notices em `docs/license/`; cada arquivo mantém a identificação de seu
componente e a referência à origem. O frontend 68000 compartilhado já é
o único caminho de recompilação ativo. Musashi continua obrigatório como
fallback interpretado para instruções ainda não cobertas e código executado em
RAM; a interpretação Z80 também permanece como fallback compartilhado, e a
migração dos módulos System 16/18 deve preservar essa cobertura.

## Estado atual

- Perfis de jogo separados do carregador: `shinobi` declara System 16A, `shinobi5` declara Shinobi System 16B não protegido, e Golden Axe tem perfis próprios.
- Carregador de ZIP valida tamanho e CRC32 e remonta as ROMs `ROM_LOAD16_BYTE` na região de programa.
- Frontend 68000 baseado no NeoGeoRecomp (código MIT em `core/third_party`): o perfil Shinobi atual gera 1.821 rotinas C e 18.252 entradas de despacho; um dispatcher nosso retoma PCs de instruções traduzidas. `run-full` avança o jogo por ciclos guest, agenda IRQ4/vblank a 60 Hz usando o clock MAME de 10 MHz e devolve o controle na fronteira das instruções quando há evento pendente. A temporização de vídeo segue específica por família: o runner System 18 já avança o VDP em scanlines; o agendamento de outros dispositivos ainda é simplificado.
- Carregador gráfico Shinobi 16A valida tiles/sprites por tamanho e CRC, remonta o interleave dos sprites e decodifica 8.192 tiles 8×8 em 3bpp. A composição usa quatro fontes de imagem — background e foreground com seleção entre oito páginas virtuais, texto e sprites 4bpp — e categorias de prioridade para ordenar os passes e misturar sprites por pixel. Capturas do título (frame 300), attract (frame 600) e gameplay (frames AVI MAME 842, 902, 1082, 1502, 1802 e 2700–2702) coincidem pixel a pixel com referências MAME. Nos estados capturados em 900, 1080, 1500, 1800 e 2700, tile RAM e text RAM coincidem byte a byte, incluindo mudanças do scroll global. Os índices de row/column scroll com flip seguem a fórmula do MAME, mas a sequência normal do Shinobi não ativa esses modos nem flip; ainda falta validação visual. Índices do AVI, contadores de frame da tela e IRQ4 não são diretamente comparáveis; a sequência sem input controlado não mede latência de input até a imagem. Também falta validar mais combinações de prioridade e temporização geral.
- Address space 68000 de 24 bits com ROM/RAM big-endian e mirrors, incluindo os espelhos de ROM/NVRAM usados pelo vetor inicial da pilha (`0xFFFFFF00` espelha para a NVRAM em `0xC73F00`). A PPI 8255 tem latches iniciais. O padrão é `--lives 3` e coinage 1 moeda/1 crédito. `run-full --lives 2|3|5|infinite` configura SW2:3–4. No Shinobi, `--coin-mode freeplay` liga Coin A e Coin B no SW1 (`DSW1=0x00`) e preserva o DIP de vidas do SW2; no menu, Free Play e Coin A/B são opções semânticas, sem expor as chavinhas físicas. `--coin-mode coin` volta ao padrão de 1 moeda/1 crédito e três vidas. `--dsw1 <byte>` e `--dsw2 <byte>` aceitam decimal ou hexadecimal, por exemplo `--dsw1 0x00 --dsw2 0x7c`, e têm precedência no modo CLI de diagnóstico. O caminho do System 16A segue `standard_io_r`: `DSW1` em offset de palavra par e `DSW2` no ímpar, no intervalo físico `0xc42000–0xc43fff`; o log final mostra os valores efetivos e contagem de leituras. Inglês, dificuldade normal e projéteis lentos permanecem como padrões dos outros DIPs. Os eventos `--button-event` cobrem Coin 1/2, Service, Start 1/2 e todos os botões e direções dos dois jogadores; `--input-event` continua disponível para máscaras brutas SERVICE/P1/P2. Eles seguem o contador físico de vblank, separado da contagem de IRQ4 aceitas. O mapeamento segue `standard_io_r` e `system16a_generic` do MAME.

Exemplo de uma partida em modo de créditos, com Coin 1, Start 1, movimento para a direita e dois botões:

```sh
./build/mamerecomp run-full shinobi /path/to/shinobi.zip --cycles 150000000 \
  --coin-mode coin \
  --button-event 120:coin1:1 --button-event 130:start1:2 \
  --button-event 700:p1_right:300 --button-event 700:p1_button1:300 \
  --button-event 800:p1_button2:45 --frame-out /tmp/shinobi-gameplay.ppm
```

`--button-event` usa `<frame>:<nome>:<duração-em-frames>`. Nomes: `coin1`, `coin2`, `service_mode`, `service1`, `start1`, `start2`; `p1_button1/2/3`, `p1_up/down/left/right`; e os equivalentes `p2_*`. `--input-event` usa `<frame>:<service|p1|p2>:<máscara-ativa-baixa>:<duração-em-frames>`; a máscara aceita decimal ou `0x` hexadecimal. Eventos simultâneos na mesma porta combinam bits ativos por AND. Com eventos configurados, o resumo mostra as leituras do 68000 e as leituras feitas enquanto havia algum botão pressionado.

Shinobi A (`shinobi`), Shinobi B (`shinobi5`) e Golden Axe (`goldnaxeud`) têm perfis separados. Os perfis seguem as declarações `ROM_START` e `GAME` do MAME (arquivos, regiões, offsets, CRCs e relação parent), consultadas em `src/mame/sega/segas16b.cpp`; elas são transcritas para C++ e não são lidas em runtime. Ao carregar um clone, o loader usa primeiro os arquivos do ZIP do clone e procura no ZIP parent apenas os arquivos que faltarem, como nos conjuntos merged. O caminho 16B de Shinobi5 usa mapper/ROM board 171-5358; Golden Axe usa 171-5797. O som System 16B agora tem Z80 interpretado, YMFM/YM2151 e uPD7759 em modo slave com FIFO, pedidos NMI/DRQ e decodificação ADPCM. A fidelidade audível e de temporização do uPD7759 ainda precisa ser conferida no jogo. O set original `goldnaxe` depende também da interface do 8751. O IRQ4/vblank atual segue sendo um agendador simplificado sem rasterização de scanlines.

O perfil `shdancer` usa a ROM board 171-5873B / `ROM_BOARD_171_SHADOW`: região 2 sem uso e região 1 espelhando a VDP por `0xC00000–0xDFFFFF`. `mwalkd` adiciona Moonwalker bootleg descriptografado na board 171-5874, que coloca a VDP na região 2; o set inclui o firmware I8751 `315-5437.ic4`. Ambos têm perfis e tabelas 68000 separadas dentro do módulo System 18. O 315-5313 tem modelo comportamental de portas, registradores, VRAM/CRAM/VSRAM, DMA, IRQ e renderização Mode 5 inicial (planos A/B, Window e sprites), ligada aos quatro pontos de mistura `vdp_mixing` do driver MAME. Shadow Dancer mantém CNT2 baixo nos estados observados. O runner reconhece e valida a ROM do I8751 de Moonwalker, mas o MCU ainda é um stub: instruções, IRQ e comunicação com o 315-5195 não são executados. A captura ativa do VDP em `mwalkd` ainda precisa ser validada no MameRecomp. O áudio System 18 executa Z80, dois YM3438/YMFM e o modelo inicial dos 8 canais RF5C68; ainda requer validação auditiva e refinamento temporal. Detalhes estão em [SYSTEM18_VIDEO_PROGRESS.md](docs/systems/sega/system18/VIDEO.md) e [AUDIO_PROGRESS.md](docs/hardware/audio/STATUS.md).

## Critérios de implementação

- Sem HLE ou substituição de rotinas do jogo por resultados inventados. CPU, mapa de memória e dispositivos devem reproduzir o comportamento documentado do hardware.
- Usar o MAME como referência de especificação e validação, não como biblioteca de execução. ROMs e dumps locais servem como entrada para análise e testes; o MAME não será embutido no executável final.
- Avançar por camadas verificáveis: semântica das instruções e flags; barramento/dispositivos; execução do programa; vídeo; áudio e entradas. Uma tradução que compila não significa que o jogo roda.

## Pipeline gráfico atual

O Shinobi não envia comandos de OpenGL/DirectX. O 68000 escreve tilemaps, sprites, paleta e registradores de vídeo; o hardware System 16A transforma esses dados em pixels. A referência local é o driver e os dispositivos de vídeo do MAME, sem dependência de execução do MAME.

```text
ROMs gráficas
    -> decodificação de tiles e sprites
    -> VRAM e registradores System 16A atualizados pelo jogo
    -> compositor OpenGL experimental: tilemaps/texto -> índices e prioridades
    -> sprites e mistura na CPU -> paleta
    -> framebuffer indexado 320×224
    -> janela SDL3 a 60 Hz ou captura PPM para inspeção
```

O driver do Shinobi compõe quatro fontes: background, foreground, texto e sprites. Background, foreground e texto têm duas categorias de prioridade usadas em passagens de desenho; isso não cria camadas ou bitplanes adicionais. Tiles e texto usam 3 bitplanes (8 valores de pen); sprites usam 4bpp (16 valores). O dispositivo System 16A expõe oito páginas virtuais de tilemap, selecionadas pelas camadas de background/foreground. A área visível do MAME é 320×224 a 60 Hz. O backend OpenGL experimental carrega os tiles decodificados num atlas inteiro e usa um compute shader para compor background, foreground e texto. Ele devolve índices de paleta e prioridades para a RAM da CPU; sprites, mistura, conversão de paleta e captura PPM continuam na CPU. O backend CPU permanece como referência de comparação.

`run-full --window` abre o menu de configuração do Shinobi antes de iniciar quando `--cycles` não foi especificado. As abas oferecem configurações do jogo/coinage, compositor Software ou OpenGL, modo janela/tela cheia e escala inteira de 2× a 5×, áudio, volume mestre e panorâmica estéreo Game/Spread, além dos controles atuais. A interface usa Dear ImGui 1.91.9b com backends SDL3/SDL_Renderer3 incluídos em `core/third_party/imgui` (MIT; proveniência em [`docs/third_party/imgui/UPSTREAM.md`](docs/third_party/imgui/UPSTREAM.md), licença em [`docs/license/imgui/LICENSE.txt`](docs/license/imgui/LICENSE.txt)), sem pacote ImGui separado no sistema. As escolhas são salvas automaticamente em `~/.config/mamerecomp/shinobi.ini` (ou no diretório XDG/APPDATA equivalente). `--cycles` e overrides brutos `--dsw1`/`--dsw2` mantêm as execuções de diagnóstico sem menu. O botão INICIAR JOGO começa a emulação; Esc fecha o menu. Para o Golden Axe 16B, `--window` abre diretamente o jogo até existir um menu com opções próprias da placa. Em jogo, a janela SDL3 apresenta o framebuffer em cada vblank físico, com pacing de 60 Hz e escala nearest preservando os 320×224 pixels do hardware. Fechar a janela ou pressionar Esc encerra. Os controles ao vivo são: W/A/S/D para direções de P1, K/L/J para os botões 1/2/3 de P1, teclado numérico 3 para o botão 3/ninjutsu de P2, 5 para Coin 1 e 1 para Start 1. Eventos `--button-event` e `--input-event` continuam combináveis com o teclado. O contexto OpenGL oculto serve aos compute shaders e é reativado a cada frame após a apresentação SDL. O relógio guest avança até o próximo vblank e a apresentação espera a cadência de 60 Hz sem alterar a contagem de ciclos. Com `--video-profile --cycles <ciclos>`, o resumo também informa tempos médios/máximos por frame da composição e da chamada ao presenter (incluindo pacing).

Áudio usa caminhos próprios por revisão. System 16A executa Z80, YM2151/YMFM e uPD7751/MCS-48 com DAC. System 16B executa Z80, YM2151/YMFM e uPD7759 slave com FIFO, NMI/DRQ e decodificação ADPCM. System 18 mantém Z80, dois YM3438, PSG Sega do VDP e RF5C68 em execução mesmo quando o playback está mudo. Use `--window --audio` para ouvir; os sons System 16B ainda precisam de comparação auditiva no jogo e contra o MAME. O ganho YM2151 segue 0,43 do driver MAME. O SDL preserva as saídas L/R do chip e o volume mestre. `--audio-pan game` mantém o pan programado pelo jogo; `--audio-pan spread` força os canais FM pares à esquerda e os ímpares à direita nos dois caminhos de áudio. O subsistema avança em fatias de até 1 ms do relógio guest. Sem `--window`, `--audio` ainda executa e registra contadores, mas não abre a saída de áudio.

## Debugger 68000

`run-full --debugger` abre um monitor interativo antes da primeira instrução recompilada. Ele oferece breakpoint e step por instrução 68000, leitura segura de ROM/RAM, inspeção dos registradores e watchpoint de escrita. Leituras de MMIO não são executadas pelo comando de memória.

```sh
./build/mamerecomp run-full shinobi /path/to/shinobi.zip --cycles 150000000 --debugger
```

Comandos: `c` continua, `s` avança uma instrução, `b <endereço>` define breakpoint, `bl` lista breakpoints, `w <endereço> [tamanho]` observa escritas, `wl` lista watchpoints, `r` mostra registradores, `m <endereço> [tamanho]` lê memória e `q` encerra a execução. Endereços e tamanhos aceitam decimal ou hexadecimal com prefixo `0x`. O código gerado precisa ser regenerado com esta versão do emitter para incluir os hooks; desligado por padrão, o monitor não recebe callbacks durante uma execução normal.

Para comparar a linha temporal da CPU sem parar interativamente, use `--cpu-trace <arquivo> --cpu-trace-cycles <início>:<fim>`. O intervalo é inclusivo e expresso em ciclos guest decimais; o TSV registra fronteiras de instruções (PC, SR, D0–D7, A0–A7), leituras e escritas de barramento nesse intervalo. Leituras e escritas mostram o PC da instrução que as realizou. Exemplo:

```sh
./build/mamerecomp run-full shinobi /path/to/shinobi.zip --cycles 164500010 \
  --cpu-trace /tmp/shinobi-cpu.tsv --cpu-trace-cycles 164499000:164500010
```

O timestamp atual é por instrução: o custo do opcode é contabilizado no começo da tradução e os acessos à memória dessa instrução aparecem no mesmo ponto de ciclo. O core 68000 do MAME pode suspender e retomar instruções em certos limites de acesso ao barramento; por isso, este TSV não representa timestamps de barramento ciclo a ciclo nem implica que qualquer diferença de fase seja erro de resultado da CPU.

## Compilar

Dependências: CMake, compilador C/C++20, libarchive e zlib. SDL3 opcional habilita `run-full --window`; SDL3 e OpenGL 4.3 habilitam também o backend compute shader. Sem eles, o backend CPU e a captura PPM continuam disponíveis.

```sh
cmake -S . -B build
cmake --build build -j1
```

## Usar

Listar perfis disponíveis:

```sh
./build/mamerecomp list-games
```

Listar os perfis Naomi registrados no módulo independente:

```sh
./build/mamerecomp list-naomi-games
```

Validar e montar a região do cartucho MvC2 a partir do ZIP MAME (sem carregar
BIOS):

```sh
./build/mamerecomp inspect-naomi-roms mvsc2u /home/luiz/git/mame/roms/mvsc2u.zip
./build/mamerecomp inspect-naomi-roms mvsc2 /home/luiz/git/mame/roms/mvsc2.zip
```

O loader confere tamanho e CRC, monta os offsets/interleaves do driver MAME,
aplica o `ROM_COPY` do set Export e procura no ZIP parent dados compartilhados
por CRC quando necessário. Para extrair, analisar, traduzir e compilar o
primeiro conjunto AOT SH-4 do `mvsc2u` usando o ZIP local:

```sh
python tools/naomi/build_mvsc2u_aot.py /home/luiz/git/mame/roms/mvsc2u.zip
```

O gerador AOT extrai e compila funções SH-4 estáticas do jogo e da BIOS USA que
é copiada para RAM. Essa etapa de geração não define o caminho de boot: o
runtime Naomi inicia pelo vetor de reset da BIOS oficial, mantém o despacho
híbrido AOT + interpretador e não usa REIOS. O comando do probe carrega
`naomi.zip` automaticamente ao lado do ROM set (ou aceita `--bios`), valida a
BIOS por CRC e mapeia seus 2 MiB como ROM. Ainda faltam partes do runtime Holly,
ARM7/AICA e PVR2; o TA ainda não rasteriza no framebuffer guest.
Os arquivos derivados da ROM ficam dentro de `build/` e não fazem parte do
repositório. Veja [NAOMI_ARCHITECTURE.md](docs/systems/sega/naomi/ARCHITECTURE.md) para a medição
atual e os limites conhecidos. Para um avanço controlado da CPU recompilada
sem vídeo, use o probe:

```sh
./build/release/mamerecomp run-naomi-sh4 mvsc2u /home/luiz/git/mame/roms/mvsc2u.zip --instructions 1000000
```

O probe executa a BIOS Naomi oficial pelo dispatcher SH-4 híbrido até o
orçamento ou até uma condição de parada. Use `--interpreter-only` para
desabilitar a busca AOT em uma execução controlada. Os checkpoints atuais
confirmam instruções da BIOS, mas ainda não comprovam o boot do jogo nem uma
imagem renderizada.

Inspecionar ROMs/vetores:

```sh
./build/mamerecomp inspect shinobi /home/luiz/git/mame/roms/shinobi.zip
```

Reconstruir e decodificar as ROMs gráficas locais do set:

```sh
./build/mamerecomp extract-graphics shinobi /home/luiz/git/mame/roms/shinobi.zip --out /tmp/shinobi-graphics
```


Gerar todas as rotinas C a partir da ROM local:

```sh
./build/mamerecomp translate-full shinobi /home/luiz/git/mame/roms/shinobi.zip --out build/generated/shinobi
```

Sega translations skip the whole-ROM pointer scan by default because it treats
many data words as code and can inflate generated source dramatically. The
runtime keeps the 68000 fallback for code that was not translated. Pass
`--pointer-scan` to opt into the full scan when investigating coverage.

Quando `run-full` encontrar um PC sem rotina gerada, acrescente esse endereço como entrada e regenere a tabela. O argumento pode ser repetido e aceita decimal ou hexadecimal (`0x...`):

```sh
./build/mamerecomp translate-full shinobi /home/luiz/git/mame/roms/shinobi.zip \
  --entry 0x1490c --entry 0x14934 --entry 0x14912 --entry 0x1495a --entry 0x148ee \
  --out /tmp/mamerecomp-full-run
```

Para incluir essa saída local no executável, configure um build separado apontando para a pasta gerada. Os arquivos são derivados da ROM e devem permanecer locais, fora de commits/distribuição:

```sh
cmake -S . -B build-shinobi -DMAMERECOMP_GENERATED_DIR="$PWD/build/generated/shinobi"
cmake --build build-shinobi -j2
./build-shinobi/mamerecomp run-full shinobi /home/luiz/git/mame/roms/shinobi.zip --cycles 1000000
```

Para colocar vários jogos no mesmo executável Release, agrupe as traduções por revisão. O host continua lendo os ROM sets dos ZIPs originais; os módulos de placa contêm as tabelas 68000 e são carregados sob demanda conforme o perfil selecionado:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DMAMERECOMP_SYSTEM16A_GENERATED_DIRS="$PWD/build/generated/shinobi" \
  -DMAMERECOMP_SYSTEM16B_GENERATED_DIRS="$PWD/build/generated/goldnaxeud"
cmake --build build/release -j2
```

Isso gera `mamerecomp`, `libmamerecomp_cpu_m68000.so`, `mamerecomp_system16a.so` e `mamerecomp_system16b.so` no diretório de build. Ao iniciar Shinobi, só o módulo 16A é aberto; Golden Axe abre o módulo 16B. O menu, a janela, a entrada e a leitura dos ZIPs permanecem no host. Nesta primeira etapa, o runtime de vídeo/áudio/barramento ainda não foi movido para os módulos.

Selecionar o backend OpenGL para tilemap/texto e conferir os índices/prioridades contra o compositor CPU:

```sh
./build-shinobi/mamerecomp run-full shinobi /home/luiz/git/mame/roms/shinobi.zip \
  --cycles 449800000 --frame-out /tmp/shinobi-opengl.ppm --video-backend opengl \
  --video-compare-cpu --video-profile
```

Para Golden Axe bootleg, o mesmo `--video-compare-cpu` compara a composição OpenGL 16B (incluindo o mixer de sprites) ao compositor CPU 16B. Para o primeiro teste interativo, deixe o áudio desligado:

```sh
./build/mamerecomp run-full goldnaxeud /home/luiz/git/mame/roms/goldnaxeud.zip \
  --window --video-backend opengl --video-compare-cpu
```

O compute shader exige OpenGL 4.3. `--video-backend cpu` é o padrão; `-DMAMERECOMP_ENABLE_OPENGL_VIDEO=OFF` desativa o backend opcional. O programa imprime o renderer OpenGL selecionado. `--video-compare-cpu` compara índices, prioridades e pixels de sombra para o mesmo estado em ambos os renderizadores 16A e 16B; combinado com `--frame-out-at-vblank`, compara exatamente o snapshot daquele vblank, mesmo que a execução continue depois. A primeira chamada inclui criação do contexto e compilação do shader; consulte [VIDEO_PROGRESS.md](docs/hardware/video/STATUS.md) para o checkpoint de paridade e os tempos medidos.

`run-full` executa o código 68000 gerado e pode gravar uma captura com `--frame-out <imagem.ppm>`. Para capturar um estado durante a execução em um vblank físico específico, combine `--frame-out` com `--frame-out-at-vblank <n>`; a captura ocorre antes de aceitar o IRQ4 daquele vblank. O runner System 18 usa os 262 scanlines e 57,23 Hz do MAME; as leituras/escritas VDP atualizam H/V a partir do ciclo guest, embora HBLANK e bordas de pixel/IRQ ainda não sejam dot-accurate. Use `--video-state-out <diretório>` para guardar RAM de vídeo, NVRAM/RAM de trabalho, gráficos decodificados e registradores. `--cycles` limita a execução guest; o resultado também mostra registradores, PC, SP, ciclos, instruções e despachos, ou o primeiro erro no mapa. A janela SDL3 e a entrada WASD/K/L estão disponíveis com `--window`; `--audio` habilita playback da placa do perfil: YM2151 + uPD7751/DAC no Shinobi, YM2151 + uPD7759 no System 16B, e dual YM3438 + PSG Sega + RF5C68 no System 18. System 18 emula seus dispositivos de som mesmo sem playback.

Para medir o pipeline do próprio recomp, acrescente `--video-profile` a `run-full`; com `--frame-out`, o relatório separa decodificação de tiles, composição por fonte, simulação da CPU e gravação RGB/PPM, além de contar lookups e pixels processados. Esses tempos de parede não usam os contadores de IRQ4 ou frames do MAME. Os resultados e a captura de referência do frame 600 estão documentados em [VIDEO_PROGRESS.md](docs/hardware/video/STATUS.md).

Os aprimoramentos adiados e as próximas validações estão listados em [TODO.md](docs/TODO.md).

## Arquitetura planejada

1. Validar semântica do frontend 68000 executando código guest e comparando traces com Musashi/MAME quando os cenários estiverem disponíveis.
2. Implementar e validar handlers de I/O e temporização dos dispositivos System 16A; refinar a fase do IRQ4/vblank junto ao vídeo/scanlines.
3. Implementar o mapper 315-5195 da System 16B com perfis de configuração por jogo.
4. Integrar vídeo, áudio, entradas e interrupções; adicionar perfis de ROM por jogo.
5. Tratar FD1094 e `shinobi1` depois que o caminho sem criptografia estiver funcionando.

A camada de vídeo permanece específica por revisão. O loader/decode de tiles atual é para o set System 16A. Nos boards 16B, os chips 315-5196 (sprites) e 315-5197 (tilemaps) exigem implementação/configuração própria; não se presume que esses componentes físicos existam na 16A. Veja [VIDEO_PROGRESS.md](docs/hardware/video/STATUS.md).
