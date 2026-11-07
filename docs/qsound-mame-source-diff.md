# QSound: diferenças entre MameRecomp e o source do MAME

Data da auditoria: 2026-11-07  
MameRecomp: `43c8698` mais alterações locais não commitadas.  
MAME: `/home/luiz/git/mame`, commit `b2a182f0`.

## Escopo e critério

Este relatório compara o caminho QSound usado por `sfex2pa`: DSP16A LLE,
interface de comandos do Z80, leitura de samples, captura serial e relógio de
áudio. Também registra diferenças no HLE local porque `QSoundMode::Mixed`
depende dele como fallback. Os trechos MAME citados são a referência; uma
diferença em relação ao MAME não é automaticamente um defeito de hardware, mas
é uma diferença de comportamento que precisa ser deliberada e testada.

O MAME em comparação seleciona o dispositivo LLE `QSOUND`: o source de
[`qsound.cpp:99`](../../mame/src/devices/sound/qsound.cpp:99) define
`QSOUND_LLE`; o HLE vive no dispositivo separado `QSOUND_HLE`.

Não usei o comportamento do MiSTer/RTL para decidir o que o MAME faz. Onde o
projeto segue o RTL e diverge do MAME, isso está identificado explicitamente.
Este documento é uma comparação de source, não uma prova de qual divergência
gera o chiado.

## Diferenças concretas

| Área | MAME | MameRecomp | Estado e possível efeito |
|---|---|---|---|
| Fetch inicial do firmware DSP16A | MAME debugger `dasm` sobre `:qsound:dsp` lê `0:000 = 0x0288` (`goto 0x0288`) e `0:288 = 0x51c0` (`pioc = 0x3800`). | O carregador little-endian e o DSP local dão as mesmas duas palavras e o mesmo salto. | **Paridade confirmada no vetor e no início do bootstrap.** A hipótese de que o DSP do MAME recebe `0x8802` está refutada pelo dump da região efetivamente consumida pelo DSP. Forçar big-endian gera PC `0x0802` local e não corresponde ao MAME. |
| Escrita da metade alta de A0/A1 | `dau_set_atx` e `dau_set_at` preservam a metade baixa conforme `PSW[4+dest]` ([`dsp16core.ipp:168-186`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:168)). | Usa `AUC[4+dest]` ([`dsp16a.cpp:277-291`](../core/cpus/dsp16/dsp16a.cpp:277)). | Divergência real de estado DAU. O projeto segue o fio descrito no JTDSP16. Uma captura local anterior com o mesmo NVRAM produziu PCM bit a bit igual nos dois modos para aquele trecho; não demonstra equivalência geral. |
| F2=9, incremento da metade alta | Incrementa a metade alta da fonte; a condição de retenção consulta `AUC[op_s+4]` e, se ligada, copia os bits baixos do acumulador destino ([`dsp16core.ipp:137-140`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:137)). | Consulta `AUC[op_d+4]` e retém os bits baixos da fonte ([`dsp16a_dau_math.hpp:31-44`](../core/cpus/dsp16/dsp16a_dau_math.hpp:31), [`dsp16a.cpp:323-325`](../core/cpus/dsp16/dsp16a.cpp:323)). | Divergência real. O projeto segue a interpretação do RTL. No caminho de sample do DL-1425 observado até agora, `AUC=0x02`, então AUC[4]/[5] estão limpos e essa diferença específica não preserva os bits baixos. |
| F2=11, arredondamento | Adiciona `0x8000` para valores não negativos e `0x7fff` para negativos, depois limpa os 16 bits inferiores. O próprio MAME marca a regra como incerta ([`dsp16core.ipp:144-149`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:144)). | Segue o arredondamento RTL: soma o bit 15 no campo superior de 21 bits ([`dsp16a_dau_math.hpp:20-28`](../core/cpus/dsp16/dsp16a_dau_math.hpp:20)). | Divergência real nos empates negativos. O firmware DL-1425 contém quatro F2=11 condicionais. Um replay local anterior com a regra do MAME mudou seis amostras em 771.452 por um LSB e não mudou o RMS medido: diferença confirmada, efeito pequeno naquele replay. |
| Alinhamento de P, AUC=2 | O MAME converte `P` para `u64` e desloca esse valor 2 bits à esquerda ([`dsp16core.ipp:193-205`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:193)). | Desloca o barramento de 32 bits, trunca a 32 bits e sign-extends ([`dsp16a_dau_math.hpp:60-75`](../core/cpus/dsp16/dsp16a_dau_math.hpp:60)). | Divergência real para valores negativos com bits superiores descartados. O DL-1425 usa alinhamento 2. No teste local de mesmo NVRAM, substituir a regra local pela expressão do MAME elevou o RMS de aproximadamente 10.033 para 21.468; o resultado ficou mais alto, não mais próximo do nível esperado. Isso mede uma diferença de áudio, mas não decide qual operação representa o chip físico. |
| Alinhamento de P, AUC=3 | Tratado como valor reservado; o MAME lança erro ([`dsp16core.ipp:198-207`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:198)). | Tratado como alinhamento 0 e retorna `P` ([`dsp16a_dau_math.hpp:70-76`](../core/cpus/dsp16/dsp16a_dau_math.hpp:70)). | Divergência real apenas para estado não suportado pelo MAME. Não há evidência de que o firmware QSound use AUC=3. |
| `DO K=1` | O MAME afirma a faixa documentada de K=2 a 127 ([`dsp16.cpp:838-859`](../../mame/src/devices/cpu/dsp16/dsp16.cpp:838)). | O core aceita K=1 e encerra o passe de carga sem executar o corpo outra vez ([`dsp16a.cpp:615-631`](../core/cpus/dsp16/dsp16a.cpp:615)). | Divergência somente para entrada inválida segundo a documentação do MAME. É comportamento defensivo e não explica o firmware válido conhecido. |
| Banco HLE com bit 15 limpo | `read_sample` sempre mascara `bank` com `0x7fff` e lê a ROM ([`qsoundhle.cpp:283-289`](../../mame/src/devices/sound/qsoundhle.cpp:283)). | `MameHle::sample` retorna zero se `bank & 0x8000` estiver limpo ([`qsound.cpp:55-59`](../core/chips/qsound.cpp:55)). | Divergência concreta no HLE local. Pode silenciar vozes cujo valor de banco não tenha o bit alto; precisa validar contra os comandos reais do jogo antes de atribuir impacto. Não afeta a leitura de ROM do DSP16A LLE. |
| Momento em que D002 captura o comando | No handler de escrita, o MAME empacota imediatamente o endereço recebido com o valor atual de `m_new_data` e agenda `set_cmd` no scheduler ([`qsound.cpp:147-168`](../../mame/src/devices/sound/qsound.cpp:147), [`qsound.cpp:370-395`](../../mame/src/devices/sound/qsound.cpp:370)). | O handler baixa ready imediatamente; `InstructionBoundary` sincroniza o DSP até a aposentadoria da instrução e só então entrega D002 e drena a IRQ ([`zn2_sound.cpp:58-96`](../src/zn/zn2_sound.cpp:58)). | A ordem anterior tornava a IRQ visível no último timestamp sincronizado, antes de avançar o DSP até o write. A correção remove essa entrega retroativa e aproxima o evento do limite do scheduler do MAME. |
| Modelo de agenda dos devices | Z80 e DSP16A são devices independentes sob o scheduler MAME; QSound configura DSP16A com clock derivado de 60 MHz ([`zn.cpp:590-601`](../../mame/src/mame/sony/zn.cpp:590), [`qsound.cpp:196-203`](../../mame/src/devices/sound/qsound.cpp:196)). | O projeto avança o Z80/DSP cooperativamente a partir do relógio PSX e cria pontos de yield para eventos ([`zn2_sound.cpp:114-123`](../src/zn/zn2_sound.cpp:114), [`zn_runtime.cpp:134-170`](../src/zn/zn_runtime.cpp:134)). | Diferença estrutural. O teste de 720 frames existente ficou a 13,2 ciclos DSP da razão nominal; isso afasta grande erro acumulado naquele run, mas não compara o instante de cada evento nem a primeira divergência de estado. |
| Taxa/consumo de frames | O MAME configura o stream com divisão inteira `clock()/2/1248` e atualiza-o ao concluir a palavra L ([`qsound.cpp:213-240`](../../mame/src/devices/sound/qsound.cpp:213), [`qsound.cpp:304-334`](../../mame/src/devices/sound/qsound.cpp:304)). A 60 MHz, a taxa anunciada é 24.038 Hz. | O frame-alvo é calculado pela razão fracionária `60 MHz/(2*1248)` contra ciclos Z80 ([`zn2_sound_timing.hpp:7-15`](../src/zn/zn2_sound_timing.hpp:7), [`zn2_sound.cpp:114-139`](../src/zn/zn2_sound.cpp:114)), mas o host recebe a taxa inteira 24.038 Hz ([`zn2_sound.hpp:21-22`](../src/zn/zn2_sound.hpp:21)). | Diferença real de quantização: até 27 frames por minuto em relação à taxa inteira do MAME. A duração indicada ao host também é baseada na taxa inteira enquanto a contagem gerada usa a fracionária. Ainda não foi demonstrado que isso causa o chiado. |
| Efeitos colaterais ao inspecionar ROM | `dsp_sample_r` não altera o banco quando `side_effects_disabled()` está ativo ([`qsound.cpp:286-292`](../../mame/src/devices/sound/qsound.cpp:286)). | O callback local atualiza `rom_bank` em toda leitura ([`qsound.cpp:221-235`](../core/chips/qsound.cpp:221)). | Diferença para debugger/disassembler ou leitura sem efeitos colaterais. Não é caminho normal de execução do jogo; não explica o áudio durante run comum. |

## Contratos que correspondem no source

- **DSP16A e dados:** MAME instancia DSP16A; usa dois clocks de entrada por
  ciclo de execução e uma RAM de dados com máscara `0x07ff`
  ([`dsp16.cpp:2092-2119`](../../mame/src/devices/cpu/dsp16/dsp16.cpp:2092),
  [`dsp16.h:60-90`](../../mame/src/devices/cpu/dsp16/dsp16.h:60)). O core local
  tem o mesmo quociente de clocks e RAM local de 2K palavras
  ([`dsp16a.cpp:72-109`](../core/cpus/dsp16/dsp16a.cpp:72)).
- **Multiplicação, shifts F2 à esquerda e saturação/flags de 36 bits:** as
  expressões comparadas correspondem ao DSP16 do MAME
  ([`dsp16core.ipp:44-101,104-166,212-228`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:44));
  os pontos de entrada locais estão em
  [`dsp16a.cpp:258-330`](../core/cpus/dsp16/dsp16a.cpp:258). Não encontrei
  divergência nesses itens na leitura feita para este relatório.
- **Endereçamento R/PT:** o incremento de PT preserva o nibble de página e
  envolve o offset de 12 bits; o postmodify R envolve a largura YAAU e respeita
  RB/RE ([`dsp16core.h:23-41`](../../mame/src/devices/cpu/dsp16/dsp16core.h:23),
  [`dsp16core.ipp:20-38`](../../mame/src/devices/cpu/dsp16/dsp16core.ipp:20)).
  O core local implementa o mesmo wrap e RB/RE para DSP16A de 16 bits
  ([`dsp16a.cpp:119-163`](../core/cpus/dsp16/dsp16a.cpp:119)).
- **Endereço externo de samples:** MAME mapeia o espaço DSP externo, lê
  `(rom_bank << 16) | rom_offset`, devolve o byte da ROM no byte alto e atualiza
  o banco com o offset da leitura ([`qsound.cpp:279-293`](../../mame/src/devices/sound/qsound.cpp:279)).
  Local executa a mesma sequência no callback e devolve `byte << 8`
  ([`qsound.cpp:221-235`](../core/chips/qsound.cpp:221)). Para SFEX2PA, a região
  é 4 MiB no MAME ([`zn.cpp:3410-3415`](../../mame/src/mame/sony/zn.cpp:3410));
  máscara de 24 bits e módulo por 4 MiB dão o mesmo alias para essa região
  potência de dois. A equivalência fora desse tamanho ainda não foi validada.
- **Comandos, status e IRQs da placa:** MAME usa Z80 de 8 MHz, IRQ periódica
  de 250 Hz, latch com NMI, D000-D002 para QSound, D003 para banco e D007 para
  ready ([`zn.cpp:590-598,633-663`](../../mame/src/mame/sony/zn.cpp:590)). Os
  contatos locais estão em
  [`zn2_sound.cpp:28-56,58-112,149-176`](../src/zn/zn2_sound.cpp:28).
  O Z80 compartilhado limpa a linha HOLD ao reconhecer a IRQ
  ([`z80.cpp:435-464`](../core/third_party/z80-aot/sound/z80.cpp:435)).
- **PIO e serial:** MAME retorna endereço pendente antes dos dados, limpa a
  linha DSP IRQ e posterga ready; a escrita PDX atualiza o seletor de banco
  ([`qsound.cpp:337-363`](../../mame/src/devices/sound/qsound.cpp:337)). O local
  segue a mesma ordem por evento cooperativo
  ([`qsound.cpp:237-264,301-315`](../core/chips/qsound.cpp:237)). A captura
  serial MAME inicia no OCK ascendente quando ready, FSR vazio e OSE desligado;
  PSEL escolhe canal, DO entra MSB-first e a atualização de stream acontece na
  palavra L ([`qsound.cpp:295-335`](../../mame/src/devices/sound/qsound.cpp:295)).
  O caminho normal local segue essas condições
  ([`qsound.cpp:318-374`](../core/chips/qsound.cpp:318)); alternância forçada
  de canal é só um override de diagnóstico.

## O que esse relatório conclui sobre o chiado

O source prova divergências concretas no DAU, na cadência de frames, no ponto
de captura D002 e num caso do HLE. A diferença de alinhamento `P` em AUC=2 é a
única entre as divergências listadas que já foi demonstrada alterando
substancialmente o nível do PCM do DSP16A no replay local. Ainda assim, o
experimento com a expressão do MAME aumentou o RMS e não resolveu o chiado.
F2=11 foi medido como alteração de poucos LSBs naquele replay. F2=9 e o caso
`DO K=1` não têm evidência de serem o defeito ativo do DL-1425.

Portanto, não há base neste relatório para dizer que todas as diferenças
explicam o ruído ou que o core está em paridade com o MAME. O próximo passo
conclusivo é executar o mesmo firmware e a mesma sequência de comandos com
trace de D002/D007, estado DSP antes/depois de cada instrução relevante e
palavras SDX/serial, alinhado por relógio e NVRAM. Isso permitirá localizar a
primeira divergência observável no source do MAME e reproduzi-la localmente.

## Trace de execução confirmado

A referência MAME usada nesta rodada é
`/home/luiz/git/MameRecomp/build/qsoundtest`, com ROMs em
`/home/luiz/git/mame/roms` e NVRAM em diretório temporário novo. O debugger
conseguiu focar `:qsound:dsp`, confirmando que essa execução usou o dispositivo
LLE. Capturas anteriores que iniciaram sem esse DSP no device tree e usaram
HLE não são comparáveis e foram descartadas como referência.

Arquivos de diagnóstico desta rodada: MAME
`/tmp/qsound_mame_pr_trace_fixed.tr`; runtime local
`/tmp/qsound_local_aligned_pr.csv`. No trecho alinhado em PC `0x5d8`, os
registradores de dados observados coincidem, mas `PR` difere (`0x03c3` no MAME,
`0x04b4` no local); o `return` vai corretamente para cada valor de `PR` nos dois
cores. Isso mostra que o trecho não começou no mesmo contexto de chamada e
não pode ser usado para declarar uma divergência no `return`. A palavra em
`0x5dc` é o destino imediato `0x0612` do salto condicional, não uma instrução
extra executada; ambos chegam ao alvo com custo de três ciclos.

Conclusão desta captura: ela valida a escolha do binário LLE e elimina duas
interpretações erradas do trace, mas ainda não identifica a primeira diferença
de estado que causa o chiado. O próximo trace precisa começar antes da chamada
que chega a `0x5d8` e registrar também `PR`, `PT` e `PI` nos dois lados.

### Auditoria da hipótese de endianness

Em 2026-11-07, a sugestão de trocar o loader para big-endian foi testada e
revertida. O arquivo cru começa com os bytes `88 02` (`0x8802` como inteiro
big-endian, `0x0288` como little-endian). O MAME LLE aplica em conjunto
`ROM_REGION16_BE` e `ROM_LOAD16_WORD_SWAP`; o HLE usa
`ROM_REGION16_LE` e `ROM_LOAD16_WORD`. Ambas as combinações expõem ao DSP a
mesma primeira palavra efetiva, `0x0288`. O MAME debugger, com
`focus :qsound:dsp`, executou
`dasm /tmp/qsound_mame_fw_dasm.txt,0,0x290,true,:qsound:dsp` contra a região
carregada pelo próprio dispositivo. O resultado relevante foi:

```text
0:000: 0288      goto 0x0288
0:288: 51C0 3800 pioc = 0x3800
```

O trace local little-endian tem `PC=0, opcode=0x0288`, seguido de
`PC=0x0288, opcode=0x51c0`, exatamente igual. Com a conversão big-endian
experimental, o primeiro opcode virou `0x8802`, que o core local executou como
salto para `PC=0x0802`; o DSP não chegou ao bootstrap e não gerou atividade de
QSound. Portanto esse experimento não corrige o chiado e não deve ser mantido.
O snapshot de PC observado por um tap Lua em um callback Z80 não substitui o
dump da memória de programa nem prova sozinho o opcode atualmente executado.

Uma nova captura parou no primeiro write a `D002=06` e executou o comando
debugger `time` no próprio watchpoint. O MAME reportou tempo global
`0.024080500 s`; o último registro da trilha DSP foi `TC=18137` (PC `0x057e`).
Como o DSP executa a 30 MHz de ciclos de máquina, esse contador representa
aproximadamente `0.0006046 s` de tempo local. O local já estava em cerca de
`722377` ciclos DSP quando o Z80 chegou ao mesmo write (192634 ciclos Z80,
aproximadamente 24.08 ms). Isso mostra que um breakpoint no Z80 observa o MAME
com os devices em tempos locais diferentes; isoladamente, não prova que o DSP
perdeu ciclos ou que a implementação local está adiantada. A próxima comparação
precisa observar o DSP depois da sincronização de scheduler do MAME, junto com
o commit do comando e as leituras PIO correspondentes.

### Correção do primeiro D002 antecipado

O trace local mostrou `D002=06` no Z80 em `192634`; o evento já aparecia no
DSP em `722300`, embora a instrução de escrita ainda não tivesse aposentado.
`InstructionBoundary()` estava chamando `QSound::write(2)` e `run_clocks(0)`
antes de sincronizar o DSP. Como `run_clocks(0)` drena a fila de eventos,
`set_cmd`/IRQ eram aplicados retroativamente no timestamp antigo. Agora o
limite primeiro avança o DSP e depois entrega o comando. Após a correção, o
índice do primeiro comando muda em `DSP cycle=722426`, imediatamente depois
da aposentadoria local; o tap MAME observa o write em `Z80 cycle=192644`
(`DSP cycle≈722415`). A diferença residual é de cerca de três ciclos Z80 e
deve ser interpretada no limite de sincronização, não como a antecipação de
dezenas de ciclos DSP observada antes.

O run headless de dois frames confirmou a sequência inicial de comandos e a
execução do DSP, mas não comprova a eliminação audível do chiado: sem entrada
de jogo, os samples permanecem zerados. É necessário validar áudio com a
mesma sequência de coin/start e comparar a primeira leitura PIO/estado DSP
após D002.

### Primeira diferença observada na sequência de comandos

Captura MAME sem debugger e sem polling artificial de status foi feita com taps
Lua persistidos por uma closure (o handle do tap precisa permanecer vivo; caso
contrário o destrutor remove o tap após o script de autoboot). Arquivos:
`/tmp/qsound_mame_status_tap.csv` e `/tmp/qsound_mame_tap_aligned.csv`.
EEPROM MAME nova, `-nonvram_save`, mesma sequência de coin/start/botão do run
local; o runtime usou `/tmp/qsound_local_aligned_fresh.at28c16`, inexistente
antes do teste.

No primeiro comando, MAME escreve `D002=06` em 192644 ciclos Z80 equivalentes
(0,024080500 s × 8 MHz). As leituras passivas de `D007` ocorrem em 192664,
192693 e 192722; a terceira já retorna `0x80`. O próximo `D001=00` ocorre em
192750, com PC Z80 `0x0b9e`. A sequência de endereço/dados é a mesma do runtime.

No runtime anterior, o DSP só avançava em limites de slice; o Z80 lia status
obsoleto até o próximo limite e só escrevia o próximo `D001` em 193059. Foi
adicionado `sync_qsound(cpu_.BusCallbackCycles())` antes da leitura passiva de
`D007` em [`zn2_sound.cpp`](../src/zn/zn2_sound.cpp). Com isso, o primeiro `D001`
local passou para 192856. Os polls locais passam a avançar o DSP no timestamp
do Z80, mas o status ainda fica ocupado até 192841, contra 192722 no MAME.
Portanto a primeira divergência observável agora é o tempo de resposta do DSP
ao comando `0x06` (cerca de 119 ciclos Z80 após polls alinhados), não a seleção
do modo LLE nem a sequência de payload.

Esse resultado ainda não prova que o atraso seja a causa do chiado. O próximo
alvo é o primeiro desvio de estado do DSP durante essa resposta: MAME mostra
`PC=0x051e`, `0x0525`, `0x056d` nos três polls; o trace local mostra
`0x05b6`, `0x0612`, `0x050b` nos polls equivalentes antes de retornar ao vetor
`0x0005`. Comparar a aceitação da interrupção e a leitura PIO no source DSP16
do MAME contra o core local deve localizar o motivo desse atraso.
