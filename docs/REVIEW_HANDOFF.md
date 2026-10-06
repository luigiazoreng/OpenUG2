# Handoff para revisão independente — OpenUG2 / PS Vita

Leia este arquivo antes de alterar o projeto. O usuário pediu revisão de tudo
que foi preparado até a homologação técnica da base no host. A próxima issue
da fila, #14, ainda não foi iniciada. Este documento entrega o estado para
revisão; os resultados registrados abaixo não substituem a revisão independente.

## Objetivo e decisões do usuário

Port nativo da engine C99 para PS Vita com SDL2/vitaGL/VitaSDK, lendo os dados
originais de PC fornecidos pelo usuário. Primeiro milestone de produto: 240SX
dirigível em STREAML4RA no console, WorldResident ativo, 960x544, alvo de 30 FPS
e física a 60 Hz. Esse milestone ainda não foi alcançado.

A decisão posterior ao pedido inicial foi trabalhar sobre um commit fixado de
`development`; contribuições futuras devem mirar `development`. Não sincronizar
automaticamente com a ponta do upstream. Uma issue por branch, execução de uma
frente por vez. Comentários públicos somente depois de finalizar, validar e
homologar a respectiva entrega. A revisão atual não autoriza push, PR ou postagem.

Os dados de varejo, executáveis EA, mídias e capturas locais devem permanecer
fora do Git/VPK. Preserve os relatórios históricos; não sobrescreva suas matrizes
com resultados de outra versão da engine. Testes devem respeitar os limites de
memória abaixo.

## Checkouts e versões

Workspace principal: `/home/luigi/Documents/luigi/OpenUG2`.
Branch de integração: `integration-development`.
Código entregue para homologação: `addbcbe2a8d3ec6911004fad418443f96c9ebe1f`.
Commits posteriores nessa branch registram documentação e o inventário.
Tag local da entrega: `homologation-host-2026-10-05`. Ela fixa código e handoff
para revisão e não representa um release do port.

| Frente | Branch / revisão | Checkout e escopo |
|---|---|---|
| Vita inicial | `vita-bringup`, `8315923` | Referência preservada; bootstrap `8a98944`, coordenação `5fd96a8`, política `8315923` |
| Cobertura #7 | `issue-7-stream-coverage`, `7e95be5` | `/home/luigi/Documents/luigi/OpenUG2-issue7`; base antiga `84ab8b3` |
| Correção de tráfego | `fix-traffic-contact-regression`, `2a84368` | `/home/luigi/Documents/luigi/OpenUG2-traffic-contact`; base `6c9dd09` |
| Avisos, limites e saída do decoder | `fix-compiler-warnings`, `d2c92e6` | `/home/luigi/Documents/luigi/OpenUG2-compiler-warnings`; commits `762631c` e `d2c92e6`, sobre `2a84368` |
| Integração atual | `integration-development`, código `addbcbe` | Checkout principal; bootstrap + development + correções portáveis |
| Referência upstream | `6c9dd09`, detached | `/tmp/openug2-development-6c9dd09`; referência sem as correções locais |

Upstream fixado: `6c9dd091ee3297dea898c78764edf209657ce3d5`.
O `main` observado em `59c9263dd0299a6f41827caca6536005e4a32cf2` tinha a mesma
árvore desse development naquele fetch. O merge local `9eea351` combina
`8315923` e `6c9dd09`. Correspondências de cherry-pick:

| Branch portável | Commit integrado |
|---|---|
| `2a84368` — contato com parede | `37aa070` |
| `762631c` — avisos e limites | `622a199` |
| `d2c92e6` — saída de decoder após falha | `addbcbe` |

As branches portáveis não carregam o bootstrap Vita. A #7 não foi incorporada
à integração atual: seus contadores, ferramentas e medições pertencem à base
antiga. Não aplicar seus commits sem revisar o delta entre as versões.

## Entregas existentes

**Dados locais.** `/home/luigi/Documents/luigi/nfsu2-data/` contém mídias/ISOs
preservadas, extrações em `media/cd1` e `media/cd2`, runtime em `game/`, scripts
locais de preparação e `reports/`. A preparação não executou o instalador/jogo
original. O manifesto registra 1248 arquivos de runtime e 1802353048 bytes,
origem, hashes e verificações de CRC. Use `game/` como data root. Consulte o
README dessa pasta e `reports/data-manifest.json`; não reextraia sobre ela.

**Bootstrap Vita.** `platform/vita/` isola ciclo SDL/contexto/input, teste de
shader/VBO/IBO/textura e leitura limitada do 240SX pelo parser da engine.
O asset probe recusa arquivos maiores que 32 MiB. Makefile/CMake produzem um
VPK com `eboot.bin` e `sce_sys/param.sfo`, sem dados de varejo. Esse executável
não contém o loop completo da engine, carro dirigível ou streaming integrado.
O SDK exige SDL2/vitaGL e as dependências do tradutor GLSL. Leia `VITA_PORT.md`.

**#7, máquina A.** Commits `6cc8dc9`, `b5f4bae`, `419cf1f` e `7e95be5`: contadores
de geometria fonte/emitida, coletor schema 2, comparador e launcher Linux com
limites de memória. Oito regiões A/B/C/D/F/G/H/R passaram duas auditorias de
instância e duas de resident por pose fixa. O comparador recusa relabelar o
mesmo host como máquina independente e exige os mesmos inputs, código fonte,
Makefile, perfil e poses. HEAD/binary de outra plataforma podem diferir; veja
o contrato efetivo de `stream_coverage_compare.py`.
O documento `STREAM_COVERAGE.md` existe **no checkout da #7**, não no principal.
A segunda máquina física permanece pendente. Houve um comentário anterior na
#7, simplificado a pedido do usuário; nenhum PR foi aberto. Não interpretar as
comparações entre worktrees deste host como a segunda máquina.

**Integração de development.** O primeiro levantamento registrou os builds,
inventário de APIs, 15/16 testes passando e a falha de tráfego também reproduzida
no upstream limpo. Esses logs foram mantidos. A integração não alterou silenciosamente
a matriz da #7. Veja `DEVELOPMENT_INTEGRATION.md` para a sequência e proveniência.

**Contato com parede.** Na fila rotacionada de 15 carros, contatos repetidos
removiam parte da velocidade tangencial por arredondamento na normal da parede.
`cw_clip_z`, `cw_segment_pair` e `cw_shape_feature` mantêm precisão double até
construir a normal, com geometria/estado/interfaces públicos ainda em float.
O caso falha antes e passa depois com a tolerância original de `1e-5`.
Preservam-se verificações de energia, momento, separação, suporte e bloqueio.
O fixture adicional de parede/cantos também passa com a física antiga; ele
protege comportamento, mas não é a demonstração fail-before dessa regressão.

**Avisos e limites.** Os 186/187/190 avisos de normal/debug/GLES foram tratados
sem desabilitar novas categorias. Houve separação de statements, verificações
de truncamento, cópia de nomes com terminador, limites signed/unsigned para
chunks/texturas, ajustes de labels e argumentos não usados no fallback GLES.
AI, áudio e física nessa etapa mudaram apenas whitespace. Os testes passaram
a liberar suas tabelas TPK; são 15 fixtures antigas e duas novas.

**Achado da homologação.** `n2_tpk_decode` retornava falha após liberar RGB,
deixando ponteiro/dimensões/flags no output. A revisão acrescentou regressões
DXT/P8: antes, 359 verificações passavam e duas falhavam; depois, 361 passaram.
`d2c92e6` limpa toda a saída ao retornar falha. A mesma suíte passou ASan/UBSan
com detecção de vazamentos. A correção preserva o caminho de sucesso.

## Homologação e evidências

O escopo homologado é a base no host Linux e os artefatos compilados indicados
no relatório final. Console físico, desempenho e segunda máquina não fazem
parte da aprovação técnica deste host. O relatório da rodada final fica em:

```text
/home/luigi/Documents/luigi/nfsu2-data/reports/homologation-review-2026-10-05/
```

Aceite técnico: **APPROVED_HOST_ONLY**. Três builds completos, 17 targets
sintéticos e três targets CLI passaram sem avisos; parser 361/361, ASan/UBSan
com leaks, duas translation units ARM e VPK passaram. Contagens L4RA/L4RB e
PNGs coincidiram com a referência. Foram preservados os 121 arquivos históricos,
as branches antiga Vita/#7 e o binário da #7.
Picos amostrados: 462,7 MiB nos builds/testes; 1125,3 MiB na scope de dados reais
e 596,1 MiB de RSS do grupo. RAM disponível mínima nessa comparação: 7857,2 MiB.

Consulte `validation.json`, `decoder-before-validation.json`,
`decoder-sanitizer-validation.json`, `reference-comparison.json` e
`homologation.json`. As fases têm argv/log/returncode e observações de memória.
A rodada final verifica normal, debug completo, GLES, 17 targets sintéticos,
três targets CLI, ASan/UBSan/leaks, compilação ARM, VPK e poses L4RA/L4RB.
O valor de aceite e os hashes aprovados constam em `homologation.json`.
O manifesto `evidence-manifest.json` permite conferir a integridade dos arquivos
entregues nessa rodada sem refazer os testes.

Evidências anteriores, que devem permanecer preservadas:

| Diretório em `nfsu2-data/reports/` | Evidência |
|---|---|
| `stream-coverage-2026-10-05/` | Matriz inicial schema 1 |
| `issue7-schema2-machine-a-guarded/` | Oito regiões da máquina A, schema 2, capturas/revisão visual |
| `development-integration-2026-10-05/` | Integração original e falha reproduzida no upstream; manifesto de 121 arquivos históricos |
| `traffic-contact-regression-2026-10-05/` | Diagnóstico, fail-before, testes corrigidos, sanitizers e ARM |
| `traffic-contact-integration-2026-10-05/` | Correção integrada e comparação a poses fixas |
| `compiler-warnings-2026-10-05/` | Tratamento isolado, casos de limites, leaks das fixtures e resultados corrigidos |
| `compiler-warnings-integration-2026-10-05/` | Build/testes completos sem avisos antes do follow-up do decoder |

Logs de tentativas que falharam não são evidência de aprovação. Nos diretórios
de tratamento, use os registros finais/accepted e os resultados da última rodada.
Os scripts diagnósticos temporários fora do Git não são patches de produção.

As poses de comparação são 240SX, 960x544, heading 0, resident radius 1400 m:

| Região | XYZ fixo | SHA-256 da captura de referência |
|---|---|---|
| STREAML4RA | (-13.872, 40.967, 8.270) | `82a8948a979fb539937f812f32fff66c713ef6e3aba493c7d2017aedd402bade` |
| STREAML4RB | (490.582, 396.154, -3.143) | `44bfedab83c5a757fdf7d27046552803fb139f780e3b9a5449d9fc93cbae01e2` |

Fingerprint dos inputs de geometria/material da medição:
`7f6d7feb8540bdc99358fb187b2926d7e5c50169f26c68ae463e3a9471372046`.
Igualdade byte a byte de imagens foi usada neste host para pré/pós integração;
isso não estabelece igualdade de rasterização entre GPUs diferentes.
Tempos CPU/GPU são excluídos da comparação estrutural.

## Reprodução com limites de memória

Host observado: Linux x86_64, GCC 13.3.0; compilador Vita ARM GCC 15.2.0.
SDK em `/tmp/openug2-vitasdk`. Headers SDL externos em
`/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2` e
`.../usr/include/x86_64-linux-gnu`. Runtime SDL: `-l:libSDL2-2.0.so.0`.
Esses diretórios temporários podem precisar ser restabelecidos em outro ambiente.

Execute builds/testes sequencialmente, `make -j1`. O guard local é
`/home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py`.
Ele impõe MemoryMax via systemd/cgroup, desativa swap da scope, exige o orçamento
mais 4096 MiB disponíveis antes da fase e monitora a reserva do host. Use
512 MiB em normal/GLES/testes e 768 MiB em debug. Timeout de 300 s por fase.
Para dados reais: scope de 2048 MiB, RSS do grupo limitado a 1024 MiB, reserva
de 4096 MiB, 180 s por comando. Não usar `--track ALL` como evidência de mapa.
Interrompa a sequência em pressão de memória; não aumente limites ou tente
novamente automaticamente. Picos amostrados não são máximos exatos de alocação.

Exemplo de build normal guardado no checkout principal:

```sh
OPENUG2_VALIDATION_BUDGET_MIB=512 \
python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py \
  make -j1 normal \
  'CFLAGS=-O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function' \
  'SDL_CFLAGS=-I/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2 -I/tmp/openug2-vita-reference/desktop-sdl/usr/include/x86_64-linux-gnu -D_REENTRANT' \
  'SDL_LIBS=-l:libSDL2-2.0.so.0'
```

O `-Wno-unused-function` já existia para cópias estáticas do parser por TU.
Debug/GLES sobrescrevem `nfsu2`; restaure o normal antes das auditorias desktop.
Os runners `validate.py` e `compare-preserved-reference.py` da rodada final
registram os comandos usados. Para novos testes, copie/adapte o runner e use
um **novo diretório** de evidências, sem sobrescrever a homologação entregue.
Leia os logs primeiro; não repita cargas pesadas sem uma dúvida concreta.

## Roteiro de revisão

Comece por `DEVELOPER_GUIDE.md`, `VITA_PORT.md`, `DEVELOPMENT_INTEGRATION.md`,
`COMPILER_WARNINGS.md`, `TRAFFIC_CONTACT_REGRESSION.md`, `VITA_API_INVENTORY.md`
e pelo documento da #7 no outro checkout. Compare cada contribuição ao seu
parent e separe o delta upstream do delta local:

```sh
git show --stat 8a98944 6cc8dc9 b5f4bae 419cf1f
git show 2a84368 -- src/physics.c tools/ai_drive_test.c tools/district_collision_test.c
git show 762631c -- src/resource.c src/world_instance.c src/nfsu2.h src/main.c
git show d2c92e6 -- src/nfsu2.h tools/car_material_test.c
git diff 6c9dd09..addbcbe -- src Makefile platform/vita
git worktree list
git status --short
```

As branches de avisos dependem da correção de tráfego. Antes de preparar PRs
upstream, revisar essa dependência e a base do diff; não enviar uma correção
isolada com alterações de outra frente por acidente.

Prioridades para o parecer:

1. Revisar normal/clipping/cantos e os thresholds preservados do contato com
   parede. Verificar o custo de double como pendência de benchmark no Vita.
2. Revisar ownership e estado da saída de `n2_tpk_decode` em falha/sucesso,
   limites de offsets em 32 bits e limites parent/child em `n2_audit_meshes`.
   Esse patch não pretende endurecer todo o parser nem todas as falhas de malloc.
3. Conferir truncamento/terminação em `res_list_circuits`, cópias de campos de
   64 bytes e caminhos de auditoria/focus. Confirmar o fail-before do fixture.
4. Conferir a separação de código Vita e comum, proteção contra inclusão de
   dados de varejo, dependências CMake/SDK e conteúdos efetivos do VPK.
5. Rever contadores/coletor/comparador da #7, provenance dos reports, rejeição
   de mesma máquina e preservação das 121 evidências antigas. A matriz atual
   da integração cobre duas regiões, não substitui a matriz de oito da #7.
6. Conferir argv, flags, exit codes, sanitizers, SHA dos binários/capturas,
   restauração do perfil normal e a consistência entre fontes e relatórios.
7. Rever o inventário lexical de APIs: 82 GL diretas, cinco indiretas, 34 SDL
   e 82 libc/POSIX/math/zlib no código de produção. Declaração/exportação no
   SDK não prova execução. A classificação de ownership/API da #14 falta.

Entregue achados por severidade, com arquivo/linha, impacto, evidência e uma
reprodução pequena quando possível. Identifique lacunas de evidência e indique
se impedem homologação no host ou apenas aceitação futura no console. Registre
explicitamente se não houver achados; não conclua que o port já está pronto.

## Pendências e continuação

Não homologados: execução e instalação no Vita, shaders/FBO/DXT no console,
VRAM e pico de RAM, latência do WorldResident, desempenho da física double,
milestone do 240SX dirigível, macOS/Windows e segundo host físico da #7.
Os bloqueios confirmados do port incluem `glGetUniformfv` ausente no vitaGL
instalado e `mmap` indisponível no SDK testado. A abstração do renderer e o
backend alternativo de recursos ainda não foram implementados.

Após revisão desta entrega, a fila acordada continua: #14 (inventário de
ownership e interface C pequena), depois uniforms controlados, depois leitura
de recursos com orçamento explícito de memória, depois fixtures da #8. Cada
frente terá sua branch e seus próprios resultados. Não expandir agora para
novos backends, campaign/career/front-end ou otimizações sem medição.
