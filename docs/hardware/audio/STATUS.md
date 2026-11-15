# Áudio — MultiArcadeRecomp

## System 16B e Shinobi B — 2026-10-30

Foi criado runtime separado para o som da placa B: Z80 a 5 MHz, YM2151 a 4 MHz via YMFM, latch/IRQ do mapper, ROM bancadas e uPD7759 slave com FIFO, requests DRQ/NMI, blocos ADPCM/silêncio/repetição e saída mono misturada ao YM em ganho 0,48. Os bancos de ROM implementam os fios MAME de 171-5358, 171-5521 e 171-5797. Não reutiliza o uPD7751/MCS-48 do System 16A.

Foi adicionado perfil de Shinobi 5, variante System 16B não protegida, com ROM board 171-5358 e ROMs de áudio correspondentes. Ainda faltam os outros sets Shinobi B e a variante 171-5521.

Build `/tmp/multiarcaderecomp-goldnaxe-test` passou. Execução headless de `goldnaxeud` por 200 milhões de ciclos com Coin/Start avançou cerca de 100 milhões de ciclos no Z80, produziu 1.250.000 amostras YM, 59.325 escritas, 744 key-ons, 150 bordas NMI do uPD7759 e pico PCM 12.961. O FIFO não recebeu byte nesse trecho, portanto ainda falta comprovar um sample real: teste audível no jogo e diagnóstico de quando o handler Z80 escreve no FIFO são a próxima validação. Compilar e observar pico PCM não provam fidelidade sonora.

## Investigação da ordem das vozes — 2026-10-30

Comparei `ROM_START(goldnaxe)` e `ROM_START(goldnaxeud)` em `mame/src/mame/sega/segas16b.cpp`: ambos usam exatamente os mesmos arquivos de som, `epr-12390.ic8` (CRC `399fc5f5`) e `mpr-12384.ic6` (CRC `6218d8e7`). Portanto, a diferença não vem de uma versão bootleg distinta das ROMs de áudio.

Corrigi dois desvios no sequenciador uPD7759: o caminho da nibble baixa estava gerando um NMI/FIFO request duplicado depois do último nibble do bloco; e um byte zero encerrava a leitura mesmo antes de qualquer cabeçalho ADPCM válido. O MAME mantém o pedido que já veio do evento de nibble alta e só interpreta zero como fim após um cabeçalho válido. Build recompilado. O teste headless de 500 milhões de ciclos ainda não produziu escrita FIFO porque não acionou uma voz/SFX observável; isso não confirma a correção audível. Próxima validação é ouvir a mesma morte/voz no bootleg e no parent MAME, ou executar a cena no MAME com gravação de áudio.

O pan experimental `--audio-pan spread` agora também chega ao runtime System16B: os canais FM pares são forçados à esquerda e os ímpares à direita. `--audio-pan game` preserva os registradores de pan escritos pelo jogo. O build passou e o parser confirmou que `spread` seleciona esse modo; a diferença audível só pode ser avaliada no teste interativo.

## Primeiro caminho conectado — 2026-10-29

O caminho opcional `run-full --audio` integra o Z80 interpretado à ROM `0000–7fff`, latch `e800`, RAM `f800–ffff`, portas YM2151 (`00–3f`), comando uPD7751 (`80–bf`) e leitura do latch (`c0–ff`). O 68000 atualiza o latch pelo PPI A; PPI C controla a borda ativa-baixa de NMI; ler o latch limpa o ACK PC6. O Z80 roda até o clock proporcional de 4 MHz, com o dispatcher do 68000 parando em fatias de até 1 ms para sincronizar CPUs e FM.

YMFM (BSD-3-Clause) foi integrado para o YM2151 a 4 MHz: portas de endereço/dados, leitura de status/busy, timers, saída de controle para o uPD7751 e geração PCM estéreo. Com `--window --audio`, as amostras são enviadas ao stream SDL3 no ganho 0,43 usado pelo driver MAME. Sem janela, o mesmo caminho executa e conta amostras, sem abrir saída de áudio. O uPD7751/MCS-48 e seu DAC não executam ainda; por isso esta etapa pode produzir música FM, mas ainda não os efeitos e samples.

O build do alvo SDL3/OpenGL terminou após a integração. Ainda falta executar Shinobi com `--audio`, conferir a contagem de escritas YM2151/comandos uPD7751 e comparar timing e áudio contra o MAME. A sincronização atual limita fatias a 1 ms e executa o Z80 em fronteiras de instrução.

Execuções headless no build `build/debugger-build/multiarcaderecomp` confirmaram que o Z80 avança aproximadamente 4 milhões de T-states por segundo e que o YMFM gera 62.500 amostras/s. Em 30 milhões de ciclos do 68000 com Coin+Start houve 820 operações nas portas YM2151, um comando para o uPD7751, nenhum key-on no registrador `0x08` e pico PCM igual a zero. Portanto, a síntese FM está conectada e avança, mas a sequência testada ainda não pediu uma nota; o teste com janela/saída de áudio é necessário para descobrir se a música começa após outra fase ou se falta um comando/NMI.

A mesma execução imprimiu `Validated sound ROMs: Z80=0x8000 uPD7751=0x400 sample-data=0x8000`, confirmou uma borda NMI, uma leitura do latch (`0xb8`) e uma escrita de comando (`0xfc`) para a interface do uPD7751. Isso confirma que a ROM de áudio não está mais desabilitada quando `--audio` é passado. O MCU/DAC continua sem execução, e o YM ainda não recebeu key-on.

## Verificação após o relato de silêncio — 2026-10-30

Recompilei o alvo depois de retirar uma tentativa de encaminhar o IRQ do YM2151 ao Z80. A fonte de `segas16a.cpp` configura o YM2151 a 4 MHz e conecta a porta de controle dele ao reset/IRQ do uPD7751; ela não liga o IRQ de saída do YM2151 ao Z80. O status do chip continua sendo consultado por `IN A,(1)` no programa Z80 e deve permanecer disponível.

Uma execução headless de 300 milhões de ciclos do 68000, com Free Play, Coin/Start e controles de gameplay, carrega as três regiões de áudio e avança o Z80 e o YMFM, mas termina com `NMI-edges=1`, `latch-reads=1`, `last-command=0xfc`, `YM2151 writes=7260`, `active-voices=0`, `key-ons=0` e `peak=0`. Logo, o caminho PCM/SDL não é a causa do silêncio observado: o YM2151 ainda não recebeu um comando de key-on, e os registros de canal do próprio programa Z80 estão inativos. O latch principal no fim contém `0xa4`; rastrear como essa sequência é despachada no programa Z80 e em que ponto ela deveria iniciar música antes de voltar a testar a saída audível.

## Comparação do ymfm local e do MAME — 2026-10-30

O clone `/home/luiz/git/ymfm` está no commit `81aec25`. O README do projeto lista o YM2151 na família **OPM** (`ymfm_opm.*`) e o YM2610 na família **OPN** (`ymfm_opn.*`): a biblioteca compartilha o mecanismo de operadores, mas implementa registradores e comportamento de cada família separadamente. Os dois projetos usam o núcleo emulado/interpretado chamando `generate()` segundo o clock externo; não há recompilação de instruções do Yamaha.

Comparei o clone com `mame/3rdparty/ymfm` e com a cópia em `core/third_party/ymfm` do MultiArcadeRecomp. `ymfm_opm.h/.cpp` é byte a byte igual nos três lugares. As diferenças encontradas entre o clone atual e a cópia vendorizada do MAME estão em auxiliares comuns: escrita WAV endian-safe em `ymfm.h` e uma guarda `if constexpr` em `ymfm_fm.ipp` para instâncias com menos de 18 operadores. Para o OPM do YM2151 (32 operadores), essa guarda toma o mesmo caminho e não altera o cálculo. A versão parcial do MultiArcadeRecomp contém somente OPM e o motor comum; ela não usa o core OPN/YM2610 do Neo Geo.

O driver System 16A do MAME configura YM2151 a 4 MHz, rota FM com ganho 0,43, DAC discreto com ganho 0,4, e conecta a porta de controle do YM2151 ao reset/IRQ do uPD7751. Portanto, não encontrei diferença de versão no core YM2151 que explique a ausência de música. O diagnóstico atual aponta antes para o programa de som/estado dos canais: comando principal `0xa4`, zero canais ativos e nenhuma escrita de key-on `0x08`.

## Correção do handshake da 8255 — 2026-10-30

O Shinobi programa a 8255 com `0xc0`, selecionando modo 2 para o port A. Nesse modo, PC7 é o sinal OBF ativo-baixo gerado pelo hardware: uma escrita do 68000 no latch coloca OBF em baixo e solicita NMI ao Z80; a leitura do latch pelo Z80 confirma o comando via ACK em PC6 e libera OBF. O modelo anterior tratava PC7 como saída comum, interpretava o valor inicial `0x07` do port C como NMI e não reproduzia a sequência OBF/ACK para novos comandos.

`system16_bus.cpp` agora acompanha o modo 2, gera NMI ao escrever o port A, e libera PC7 após a confirmação de leitura. Fora do modo 2, o comportamento direto do port C continua disponível. Build `build/debugger-build` recompilado com sucesso.

Execução headless com Coin e Start em 100 milhões de ciclos: `NMI-edges=18`, `latch-reads=18`, `YM2151 writes=10855`, `key-ons=72`, `active-voices=8`, `generated samples=625000` e `peak=3219`. Isso confirma que o programa de som inicia vozes e o YMFM produz PCM não nulo. O caminho FM está pronto para teste audível com `--window --audio`; ainda falta validar o som ouvido e comparar a sequência/timing com MAME. uPD7751 e DAC permanecem pendentes, então efeitos e samples ainda não devem tocar.

O usuário confirmou a saída FM audível. Na escuta, observou sons se sobrepondo/cortando uns aos outros, embora a reprodução geral esteja funcionando. Também esclareceu a divisão percebida no hardware: os gritos de morte e alguns SFX passam pelo uPD7751; outros SFX são gerados pelo YM2151. Tratar isso como hipótese de trabalho informada pela experiência no jogo e mapear os comandos individualmente durante a implementação do MCU. Sobreposição pode ser intencional; distinguir isso de corte de voz incorreto acompanhando canal/key-on e comando Z80 no tempo.

## Saída estéreo nativa e diagnóstico por canal — 2026-10-30

O YMFM YM2151 já gera duas saídas independentes e acumula nelas os oito canais FM segundo os bits de pan dos registradores 0x20–0x27. O MAME do System 16A mistura ambas em um único speaker `front_center`, ganho 0,43; isso reproduz a configuração mono da placa. Para o teste estéreo solicitado, o runtime envia os outputs L/R nativos ao SDL, cada um no ganho 0,43, mantendo o pan do jogo. Isso é uma opção de apresentação estéreo acima da rota mono configurada pelo MAME.

O resumo da execução agora também conta key-ons por canal (bits 0–2 do registrador 0x08). Usar a distribuição e o momento desses key-ons junto dos comandos Z80 para investigar SFX que cortam outros: o YMFM mistura os oito canais internamente, então um conflito persistente pode vir da alocação/retrigger do driver Z80, e não da soma de PCM no SDL.

Execução de 100 milhões de ciclos confirmou a ausência de pan programado nesta sequência do jogo: os registradores 0x20–0x27 ficaram todos em `0xc0` (L+R habilitados) e o pico da diferença entre as amostras estéreo foi zero. Portanto o executável preserva o estéreo do chip, mas o Shinobi set 6 está mandando a mesma mistura aos dois lados. Não forçar pan artificial por canal. Isso não explica SFX que cortam outros; rastrear os retriggers e a alocação de canais Z80 separadamente.

Por pedido do usuário, foi adicionado o modo opt-in `--audio-pan spread`: nas escritas aos registradores 0x20–0x27 preserva os outros bits e substitui somente os bits de saída, alternando canais pares para L e ímpares para R. `game` continua sendo o padrão e mantém fielmente os registradores da ROM. Build passou; no mesmo teste headless, `spread` produziu diferença L/R máxima de 3325 PCM e manteve 72 key-ons/8 vozes. É expansão estéreo experimental, não comportamento original nem correção demonstrada do corte de SFX.

O teste mostrou que alternar as escritas mono do DAC entre L/R degrada os samples; essa experiência foi removida. O DAC permanece centralizado nos dois modos de pan. O YM2151 mantém ganho 0,43 do driver MAME. Após relatos de distorção/volume excessivo, o DAC foi baixado para ganho 0,20 (metade do 0,4 do driver) e recebeu um filtro passa-altas suave para retirar o offset DC do valor P1 retido durante reset/idle. Esse filtro modela o acoplamento AC do caminho analógico ausente na saída SDL; a combinação precisa de validação auditiva para confirmar que remove ruído sem alterar demais os samples.

O usuário confirmou que a mixagem e o comportamento dos SFX agora parecem iguais ao MAME, exceto por um estalo imediatamente antes do playback do DAC. A inspeção de `dac_device_base::set_value` mostrou que o MAME atualiza o `sound_stream` antes de alterar o valor retido. O runtime agora sincroniza o YM até o timestamp da escrita P1 antes de trocar o valor, e executa o MCU antes do avanço residual do YM no intervalo Z80; isso evita aplicar uma escrita P1 retroativamente a amostras anteriores. O guard contra reentrância protege o callback de CT do YM ao resetar o MCU. Build e teste headless passaram; precisa de validação auditiva do estalo. Ganho DAC temporário segue em 0,20.

Para investigar quedas de framerate relatadas durante o áudio, medi 100 milhões de ciclos headless: 0,44 s sem áudio e 2,24 s com Z80/YMFM/uPD7751 ativos. Parte desse custo vinha de chamar a sincronização do YMFM depois de cada instrução MCS-48 (cerca de 3 milhões de chamadas nessa execução). Removi essas sincronizações redundantes: as escritas P1 ainda sincronizam o stream no timestamp do evento, e o scheduler fecha o intervalo após o MCU alcançar o relógio Z80. Após recompilar, a execução com áudio levou 2,19 s. Contagens guest e pico PCM permaneceram iguais (Z80 3.844.094 instruções, MCU 3.086.857, 625.000 amostras, pico 6.606), então a mudança preserva o comportamento observado, mas o ganho de tempo foi pequeno (~2%). A medição headless não inclui renderização de janela; ainda é necessário observar FPS no modo gráfico.

## Primeiro caminho do uPD7751/MCS-48 — 2026-10-30

Adicionado um núcleo interpretado MCS-48 e conectado à ROM interna de 1 KiB do uPD7751. A ponte da placa implementa o barramento de leitura de samples, comando/banco vindo da porta Z80, D8243 com leitura/escrita/OR/AND em P4–P7, reset e IRQ acionados pela saída de controle do YM2151 e amostras de P1 somadas ao PCM do FM. O nível DAC usa conversão centrada de 8 bits e ganho 0,4, correspondente à rota do DAC do driver MAME. O DAC em si não precisa interpretar instruções: mantém o último valor digital até a próxima escrita e o converte em amplitude de áudio.

O primeiro build integrado passou. Em execução headless por 100 milhões de ciclos 68000, o Z80 fez 3.844.094 instruções, o MCU fez 3.086.857 passos de instrução, houve 3 comandos para o uPD7751 e 4.939 escritas em P1. O pico PCM subiu de 3.219 (FM isolado no checkpoint anterior) para 13.685 com o caminho de DAC conectado. Isso demonstra atividade do firmware e da soma PCM, mas pico não prova por si só que os samples estejam corretos nem substitui escuta no jogo. Próximo passo: rodar com `--window --audio`, acionar um grito/SFX conhecido e comparar a sequência/tempo com o MAME. Auditar em especial as instruções MCS-48 efetivamente usadas, as linhas CT do YM2151 e o comportamento D8243/endereço de sample; o núcleo ainda é inicial e não deve ser chamado de equivalente completo ao MAME.
# System 18 audio progress — Shadow Dancer

## First integrated path — 2026-10-30

`run-full shdancer ... --audio` now instantiates an 8 MHz interpreted Z80 with the MAME sound map, fixed 0x0000–0x9fff ROM, 8 KiB bank window at 0xa000–0xbfff (bank pages are 0x2000 bytes), 8 KiB RAM at 0xe000–0xffff, and the mirrored 0xa0 bank-select port. The 315-5195 PBF write from the 68000 sets the sound latch and pulses Z80 NMI; sound-side port 0xc0 reads it and writes the reply latch back for 68000 register 3. YM3438 #1/#2 are at ports 0x80/0x90, clocked at 8 MHz through the YMFM OPN implementation. YM1 IRQ is routed to Z80 IRQ0; YM timers and busy status are synchronized against sound CPU clocks.

The RF5C68 first-pass behavioral model provides the mirrored register window, 64 KiB sample RAM, 8 channel registers, active-low channel enable, loop marker, fixed-point address stepping, per-channel pan/envelope, and 10-bit output at 10 MHz / 384. The sound output follows MAME's System 18 mono route for both YM chips (0.40 each) and RF5C68 (1.0), duplicated to SDL stereo because the board route is mono.

Build succeeded with `cmake --build build/release -j2`. A headless 100,000,000-cycle run using `shdancer.zip` advanced the Z80 to 80,000,011 cycles, wrote 3,024 / 5,362 registers to the two YM3438s, wrote 26,112 bytes to RF5C68 sample RAM, generated 555,555 YM output samples and reached nonzero FM/PCM output. The initial PCM mixer reached its clamp (combined peak 32,767; RF5C68 peak 32,704), so gain and saturation need listening/measurement before treating the mix as correct. Earlier 10-million-cycle run was still in initialization and generated zero peak.

Remaining audio work: compare sound timing against MAME and listen to attract/gameplay; audit NMI/reply semantics and YM port status behavior; refine the exact RF5C68 window, output scaling, and register synchronization against the MAME device. Shadow Dancer's audio chain is now runnable but needs user playback validation before calling it complete.

## RF5C68 level adjustment — 2026-10-30

The user reported that Shadow Dancer's sampled voices, dog bark, and glass-breaking effects were painfully loud while the rest of the audio sounded good. These sounds come through the RF5C68 PCM path, so the runtime now attenuates only that chip's output route by 50% before adding it to the YM3438 mix. The RF5C68 channel registers and raw chip output remain unchanged; the Yamaha music/effects retain their current level. MAME configures the RF5C68 route at 1.0, so this is a local mix adjustment prompted by listening, not a claim of matching MAME's gain. The next listening check should confirm the PCM effects are comfortable and still clear against FM.

The existing `--audio-pan spread` option now also applies to System 18. It alternates the 12 YM3438 channels across left/right by overriding only their OPN pan bits in registers B4–B6 on both chips, and preserves the two YMFM output sides in the SDL mix. The default `--audio-pan game` path keeps the game's register pan and the MAME mono routing. RF5C68 pan registers remain under game control in both modes. This spread is a presentation option, not board-accurate stereo; listen in game before treating its balance as final.
