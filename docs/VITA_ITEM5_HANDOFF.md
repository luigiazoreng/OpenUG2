# Handoff — engine e integração Vita até o item 5

O terceiro teste físico ficou bloqueado no link do shader, antes do input.
A nova branch de startup, a pesquisa em fontes oficiais e o monitor de saída
estão em [VITA_STARTUP_HANG.md](VITA_STARTUP_HANG.md). O reteste ainda é
necessário; os registros de preparação abaixo são históricos.

Atualização após a segunda sessão física: o guard do link evitou o data abort
e recuperou o erro do shader; o viewer encerrou com erro controlado, sem frames.
Falharam shader, DXT3/DXT5 nativos e mipmaps. A correção em branch separada e
seus critérios de homologação estão em [VITA_GL_COMPAT.md](VITA_GL_COMPAT.md).
O crash original permanece documentado em
[VITA_SHADER_CRASH.md](VITA_SHADER_CRASH.md). Os resultados abaixo descrevem
a preparação original e não aprovam automaticamente o novo pacote.

Estado em 2026-10-06: implementação e validação no host concluídas; pacote do
240SX estático preparado para homologação física. O carro dirigível em
STREAML4RA, streaming, física a 60 Hz e desempenho sustentado no Vita ainda
não estão entregues. Não há PR, push ou comentário público desta rodada.

A base continua fixada em upstream development
`6c9dd091ee3297dea898c78764edf209657ce3d5`. A tag anotada
`homologation-host-2026-10-05` continua apontando para commit `6414e1a`, com
código homologado `addbcbe`. O parecer `APPROVED_HOST_ONLY` pertence àquela
entrega; não aprova automaticamente este executável Vita.

## Entregas separadas

| Frente | Branch | Entrega / limites |
| --- | --- | --- |
| Parecer TPK | `fix-tpk-review-findings` | Limites de header e limpeza de metadata, reproduções ASan antes/depois; `9b262ad` |
| #14 | `issue-14-renderer-contract` | Inventário GL e ownership + proposta C; `3150672`. Não implementa todos os backends nem encerra a issue |
| Uniforms | `fix-renderer-uniform-state` | Mirror por programa, arrays e aliases, falhas explícitas, testes stub/GL; `e8d0d3e` |
| Arquivos sem mmap | `feat-resource-read-budget` | Leitura limitada, reserva concorrente e rollback; diagnóstico otimizado `41d2a9f` |
| GPU sintética | `vita-gpu-probes` | Shader de produção, FBO/depth, alpha, DXT e mipmaps; `e03a0e1` |
| Carro real / item 5 | `vita-stock-car-viewer` | CPU loader, renderer, controles, pacote e coletor; VPK compilado em `2cb7ef8` |

A branch `integration-development` reúne as entregas acima. Os worktrees
preservam as frentes para revisão. A #7 continua na base histórica
`7e95be5`, aguardando uma segunda máquina física. A #8 não foi iniciada nesta
rodada; seus casos sintéticos de colisão continuam na fila.

## Código e dados do item 5

`platform/vita/car_scene.c` usa o parser original e dados GLOBALB para escolher
rodas stock, eixos e bitolas. Seu orçamento de arquivos é 32 MiB total / 16
MiB por arquivo. O endereço do budget permanece estável enquanto possui
buffers. Geometry é liberado após parsing; TEXTURES e GLOBALB permanecem até
upload de todas as texturas e então são liberados. Não há buffer de arquivo
vivo durante os frames.

`car_viewer.c` reutiliza `render.c` e o mirror de uniforms, desenha carro,
quatro conjuntos de rodas, vidro ordenado, piso e legenda em um mesh. Meshes,
texturas e programa têm destructor; falhas de criação fazem rollback. Payload
GPU conservador tem teto de 24 MiB. Esse teto não mede memória total do
processo, overhead do driver ou pico do shader compiler.

Dados privados em `/home/luigi/Documents/luigi/nfsu2-data/game/`:

- `CARS/240SX/GEOMETRY.BIN`
- `CARS/240SX/TEXTURES.BIN`
- `GLOBAL/GLOBALB.BUN`

O loader real encontra 101 meshes, 30.699 vertices, 18.615 indices, sete
texturas referenciadas e 24 hashes de material. Contagens do antigo bootstrap
sem catálogo de textura diferem e não são a contagem deste viewer.
CPU mesh: 1.028.718 B; pico de buffers de arquivo no host: 16.624.240 B;
payload GPU estimado: 1.860.626 B, incluindo legenda de 76.300 B; 118 draws.
As contagens de metadata e alinhamento são verificadas novamente pelo log do
Vita; tamanho de headers do budget pode diferir entre host 64-bit e ARM32.

Os materiais traseiros conhecidos pelo parser não classificam os quatro
slices BRAKELIGHT como lente emissiva confiável. A aparência das lanternas
continua limitada por essa classificação; não foi criada uma regra de hash
sem evidência. Os faróis dianteiros têm emissores conhecidos e passam o teste
visual no host. Reflexos usam o fallback analítico da engine; não há cenário
para capturar reflexão nesta cena isolada.

## Dependência e build Vita

SDK local `/tmp/openug2-vitasdk`, GCC 15.2.0, SDL2 com backend vitaGL. A lib
vitaGL instalada não suporta corretamente locations de arrays. Usa-se outra
lib, sem modificar a instalação, compilada do mesmo source
`2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8` com `NO_DEBUG=1 SAFE_UNIFORMS=1`:

`/tmp/openug2-vitagl-safe-uniforms/stage/arm-vita-eabi/lib/libvitaGL.a`

SHA-256 `fc34e03f8e6ffc4f465228139ec22aa31365496148a9d89106d4508648bd9e6c`.
Fontes, recipe fixado, comandos, disassembly e bundle recuperável estão em
`nfsu2-data/reports/vitagl-safe-uniforms-2026-10-06/`.

O SDL e a engine resolvem a mesma lib. O contexto mantém um slot de programa
reservado para evitar location fragment zero ignorada pelo setter desse
vitaGL. Isso é específico da dependência fixada e precisa dos readbacks reais
no console. GL errors não bastam: a lib usa NO_DEBUG. O common renderer não
contém stubs para falsificar readbacks.

O SDK sobrescreve CFLAGS do ambiente. A primeira integração desta rodada
compilou, mas três avisos no diagnóstico resource mostraram que o `-Werror`
solicitado via ambiente não estava ativo. O diagnóstico foi inicializado e o
CMake passou a impor `-Werror` diretamente no target. A rodada final usa
build directory novo, flags verificadas e zero avisos. Afirmações anteriores
de `-Werror` SDK baseadas apenas no ambiente não são prova dessa flag; seus
logs históricos permanecem preservados. Builds host têm flags próprias.

Comando final (envolver no lock e guard descritos abaixo):

```sh
VITASDK=/tmp/openug2-vitasdk PATH=/tmp/openug2-vitasdk/bin:$PATH \
CMAKE_BUILD_PARALLEL_LEVEL=1 make -j1 vita VITA_CAR_VIEWER=ON \
VITA_GL_LIBRARY=/tmp/openug2-vitagl-safe-uniforms/stage/arm-vita-eabi/lib/libvitaGL.a \
VITA_BUILD_DIR=build/vita-car-final-werror
```

CMake grava revisão e fingerprint das unidades C, headers locais, receita,
flags e archive. `viewer_main.c` emite essa identidade e um run ID preparado.
O sidecar associa VPK e eboot por hash. Revisão da ferramenta é separada da
revisão compilada; commits posteriores apenas de documentação não mudam o
VPK já preparado.

## Evidências e critérios de revisão

Relatórios privados; não colocar capturas/dados de varejo no Git:

| Evidência | Pasta em `nfsu2-data/reports/` | Resultado |
| --- | --- | --- |
| TPK antes/depois | `independent-review-followup-2026-10-05/` | ASan reproduz falhas antigas; correções passam |
| #14 baseline | `renderer-contract-2026-10-05/` | Builds e L4RA/B idênticos à referência |
| Resource budget | `resource-read-budget-2026-10-06/` | Limites/rollback/alinhamento/concorrência passam |
| Uniform mirror | logs versionados e `docs/RENDERER_UNIFORM_STATE.md` | Stub ASan/UBSan e oracle GL passam |
| GPU probes | `vita-gpu-probes-2026-10-06/host-final.log` | 15 PASS em GL e GLES, readbacks reais no host |
| CPU carro | `vita-car-scene-2026-10-06/` | ASan/UBSan com dados reais, falhas e lifetime |
| GPU carro | `vita-car-viewer-2026-10-06/caption-batch-final2/` | GL e GLES, dois ciclos create/draw/destroy, screenshots/ROI e zero GL errors |
| Input | `vita-item5-2026-10-06/input/` | 50 verificações SDL virtual, zero falhas |
| Build final | `vita-item5-2026-10-06/sdk/` | 9 unidades C, Werror confirmado, CRC/title/members/hash passam |
| Resource pós-integração | `vita-item5-2026-10-06/resource-budget-regression.log` | ASan/UBSan, ambos backends, duas threads passam |
| Regressão engine atual | `vita-item5-2026-10-06/engine-regression/` | Normal/debug/GLES sem avisos; L4RA/B counts e PNGs idênticos à referência |

O coletor tem 11 testes sintéticos de filesystem/USB, probes obrigatórios,
nonce e identidade compilada. Ele rejeita eboot instalado diferente, log
antigo, sidecar/config divergentes e falhas de shader/probes. Um resultado
`DEVICE_LOG_PASS_MANUAL_REVIEW_REQUIRED` ainda requer observação visual e de
controles; não equivale a homologação física.

Todas execuções de build/test pesado usam `flock /tmp/openug2-validation.lock`
e `reports/guarded-issue7-validation.py`, -j1, cgroup swap zero, reserva de
4 GiB: 512 MiB normalmente; comparação real de mapas 1536 MiB outer / 1024
MiB RSS inner. SDK final pico 74 MiB, input 37,2 MiB, resource 110,7 MiB;
builds host 456,3/463,4/445,8 MiB e comparação de mapas outer 1271 MiB.
Os mapas foram comparados no desktop original DISPLAY=:0/wayland-0; Xvfb
não fornece a mesma referência visual nessa máquina. O input e o carro
isolado podem ser testados com fixtures/Xvfb sem essa exigência de baseline.

## Pacote preparado e sessão física

Pacote exclusivo:
`/home/luigi/Documents/luigi/nfsu2-data/vita/stock-240sx-item5-2026-10-06/ready/`.
VPK: 1.250.841 B,
SHA-256 `f50415ba858e1ba754130327df3bb1df20e98da78c08421a9322e8440318ca85`.
Eboot: `3ef769e9b71974bbcb24f2de991faa2d892a5bd0414a0e9692942f93e852cfee`.
Run ID: `020f68cfb0034d28b3e230f8`.
O VPK contém somente eboot e param.sfo; dados privados são instalados
separadamente pelo bundle. Title ID OPENUG200 substitui o app de teste anterior
quando o usuário instalar pelo VitaShell, preservando arquivos em data/.

USB VitaShell identificado anteriormente: VID 054c / PID 05d3, UUID
2CDA-8AFC, montagem `/media/luigi/2CDA-8AFC`. Revalidar a identidade e montagem
antes do deploy; o console não estava exposto via USB ao preparar este pacote.
Linux não lança o bubble usando mass storage. O operador só precisa ativar
USB, instalar `ux0:data/openug2/car-viewer.vpk`, lançar e executar os controles:
left stick orbita/eleva câmera, right Y zoom, X pintura, Square faróis,
Triangle vistas, START pausa/resume, SELECT+START saída. Depois reativar USB
para o host ler o log atualizado. Não há animação de direção ou física.

Comandos em `docs/VITA_CAR_VIEWER_TESTING.md`; transferir o bundle completo,
validar readback e desmontar USB antes da instalação. Coletar em diretório
novo e registrar modelo/firmware, observações e captura física. Não publicar
comentário em issue antes de completar validação e homologação do escopo.

## Próximo trabalho depois do aceite físico

Resolver falhas concretas de probes/visual/input antes de expandir a cena.
Somente após esse aceite iniciar branch para world L4RA / WorldResident,
física 60 Hz e render 30 FPS com orçamento medido, seguido de streaming.
O design #14 continua proposta; a migração da API terá sua própria revisão.
