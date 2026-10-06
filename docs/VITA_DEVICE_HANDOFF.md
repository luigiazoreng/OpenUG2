# Handoff — Linux/Vita e diagnóstico dos controles

Data: 2026-10-05 (America/Sao_Paulo). Branch: `vita-device-tests`, worktree
`/home/luigi/Documents/luigi/OpenUG2-vita-device-tests`. Base preservada:
`integration-development`, `6414e1aea837671efcc2c1c409038053ff0919ad`, com o
[handoff anterior da engine](REVIEW_HANDOFF.md). Nenhuma alteração desta branch
foi integrada à base, enviada ao GitHub ou comentada em issue.

## Entrega e conclusão

Linux transfere VPK + somente a geometria stock 240SX para VitaShell USB/FTP,
relê/verifica SHA-256 e recolhe evidência fora do Git. USB real funciona com
SD2Vita, volume `2CDA-8AFC`, Sony `054c:05d3`. FTP foi testado em localhost;
continua sem teste no console. Instalação/abertura são manuais. Não foram
instalados plugins ou modificados `tai/config.txt`, permissões ou firmware.

O bring-up isolado mostrou triângulo texturizado e parser real no Vita.
Controles curtos podiam perder a borda down se down/up fossem consumidos
juntos; essa regressão foi reproduzida no host e corrigida. START agora
tem diagnóstico nativo/SDL e fundo azul de pausa. Cross ocupava o loop
por cerca de 2,8 segundos ao refazer o parser; a releitura passou a um único
worker CPU, deixando entrada/render ativos. Há faixa branca durante a tarefa,
backpressure para pedidos repetidos e join antes do encerramento SDL.

Evidência física da build de código `d9982c66ecf373111b200bbf713e839856ed4541`:

- quatro releituras concluídas, cinco pedidos extras ignorados e 13 pausas
  acionadas durante a leitura;
- stock 240SX: 101 meshes, 30.571 vértices, 18.237 índices, 24 hashes;
- SELECT+START simultâneos e saída limpa após 122 frames, status 0;
- oito checagens automáticas passam na coleta final;
- `eboot.bin` instalado confere com o VPK por SHA-256.

O VPK final tem 1.150.311 bytes e SHA-256
`4b841e4844622f8d5f86668b0d440dc9ade614475ca257a24229c9bd20ab98e2`.
Contém somente `eboot.bin` e `sce_sys/param.sfo`, title OPENUG200. Nenhum
dado original está no Git/VPK. O executável da engine completa não é usado
nesse teste; ela continua na base host preservada.

## Evidência para a revisão

Tudo abaixo fica em `/home/luigi/Documents/luigi/nfsu2-data/reports/`:

- `vita-device-tooling-2026-10-05/`: utilitário, seis testes USB/FTP/evidência.
- `vita-start-diagnostic-2026-10-05/`: teste de input, antes 30 pass/1 fail,
  depois 31 pass/0 fail; build e manifesto.
- `vita-cross-retry-2026-10-05/`: 17 checks de lifecycle com parser bloqueado,
  três decodes reais em worker com ASan/UBSan/LSan e build Vita zero warnings.
- `vita-device/vita-20261006T014608Z-e911f9f1/`: deployment, readbacks e handoff.
  `collection-585834bf/async-runtime-summary.json` resume a resposta durante
  leitura; `collection-7db9fb0b/vita-bringup.log` comprova a saída final.
  O `result.json` original é preservado, junto de uma aceitação por escopo.

As primeiras coletas de saída repetiam o hash anterior; reiniciar a conexão
USB forneceu o log atualizado. Considere só a coleta final citada como prova
da saída; não substitua os arquivos históricos. A causa da leitura anterior
desatualizada não foi estabelecida.

## Prioridades da revisão

Confira `platform/vita/asset_job.c/.h` e o uso em `main.c`: publicação atômica
do resultado, um parser ativo por vez, root válido até join, erro ao criar
thread, saída durante tarefa e ordem de teardown. O parser contém estado
estático; não generalize este wrapper para parsing concorrente na engine.
Worker não chama GL nem o logger. A primeira leitura antes do frame continua
síncrona. Heap de bring-up 128 MiB, arquivo máximo 32 MiB, stack do worker
128 KiB. Não há orçamento final para mapa/carro/texturas do port.

Confira `platform.c`: borda de eventos down/up preservada, evento de outro
controle ignorado, ações de pausa/saída distintas, hotplug e logs por
transição. Mapeamento SDL foi confirmado; não houve remapeamento especulativo.
Confira `renderer_vita.c`: pausa azul, faixa via scissor sem recursos extras,
scissor desabilitado após a faixa e GL usado só na thread principal.

Confira `tools/vita_device.py`: destinos restritos a data/, rejeição de
sobrescrita diferente, CRC/title do VPK, transferências em blocos, readback,
evidência exclusiva, log antigo e mensagens informativas diferenciadas de
erros. Observações visuais/controles não são fabricadas para aprovar uma coleta.

Testes reproduzíveis e passos estão em [VITA_DEVICE_TESTING.md](VITA_DEVICE_TESTING.md).
Na máquina atual o SDK é `/tmp/openug2-vitasdk` e os headers SDL do host estão
em `/tmp/openug2-vita-reference/desktop-sdl/usr/include/`. Cada compilação/teste
foi sequencial, limitado a 512 MiB via o guard documentado no handoff anterior,
com reserva de 4 GiB do host. Build final Vita: pico 138,3 MiB; decode real
sanitizado: 53,1 MiB. Arquivos temporários do SDK/headers podem desaparecer.

## Limites e próximo trabalho

Validação física cobre o bring-up descrito, não todos os controles nem o
renderer/engine completo. R e cobertura exaustiva ainda faltam; modelo e
firmware não foram informados. Join com worker ocupado foi testado no host;
a saída física final ocorreu sem worker ativo. Há cold frame de aproximadamente
1 segundo, preservado; sua causa não foi investigada. Durante os retries,
13 amostras ficaram em 27,22–30,17 FPS, sem garantia de mínimo de 30 FPS.
Pools livres não medem pico de VRAM ou RAM do processo.

O agregador conserva `REVIEW_REQUIRED` por falta dessas observações/identificação,
embora as oito checagens finais passem. O conjunto conexão/parser/pause/retry/saída
está validado; não declare homologação do port ou conclusão de V1 inteiro.
Shaders completos, DXT/FBO, carro/mapa/física/tráfego, contrato renderer #14,
alternativas de uniform/mmap, #8 e segunda máquina host da #7 seguem pendentes.
O Vita não equivale à segunda execução da matriz da engine host.
