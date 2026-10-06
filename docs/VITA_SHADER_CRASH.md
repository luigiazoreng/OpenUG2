# Vita shader crash — diagnóstico e correção

O primeiro teste físico do item 5 falhou antes de carregar os assets. O log
termina após GPU CAPS; hashes do eboot instalado, build e session run ID
coincidem com o pacote preparado em `2cb7ef8`.

Evidência preservada em
`/home/luigi/Documents/luigi/nfsu2-data/reports/vita-item5-2026-10-06/device-01/`:
log, eboot instalado, configuração, result.json, três dumps e diagnóstico.
O core completo mais recente mede 299.792 B comprimido / 5.671.436 B
expandido. Thread OPENUG200: data abort `0x30004`, PC `0xe0073c34`,
LR `0x8115a421`, R0 zero. A nota MODULE_INFO dá text runtime base
`0x81066000`; ELF original base `0x81000000`. LR rebasa para
`0x810f4421` (Thumb), logo após o BLX a
`sceGxmProgramGetParameterCount`. A linha correspondente é
`glLinkProgram`, `custom_shaders.c:2210`, que consulta `p->fshader->prog`.
O ponteiro nulo é a causa imediata do crash; ainda falta o erro do compilador
para descobrir por que o fragment shader não compilou.

## Mudanças isoladas

Branch `fix-vita-shader-crash`, worktree `OpenUG2-vita-shader-crash`.
Preservar pacote/VPK e relatórios anteriores; não reutilizar o nonce antigo.

A dependência é reconstruída do mesmo vitaGL
`2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8` em outro diretório, com
`NO_DEBUG=1 SAFE_UNIFORMS=1 HAVE_SHARK_LOG=1`. O patch versionado
`tools/vitagl-link-failure.patch` mantém verificações de attachments e
programas compilados mesmo em NO_DEBUG. No modo POSTPONED, a compilação real
acontece durante link; em falha, deixa LINK_STATUS falso e restaura o modo
antes de retornar, sem acessar metadata GXM de um programa nulo.
SDK instalado e archive SAFE_UNIFORMS anterior permanecem intactos.

O renderer oferece callback opcional de diagnóstico durante preparação:
create/source/compile/status, link/status e início dos uniforms. O viewer
liga o callback ao log persistente. Em falha de link, o renderer consulta
logs dos dois shaders ainda vivos, pois vitaGL não fornece o diagnóstico
útil em glGetProgramInfoLog. Só depois libera shaders e programa. A matemática
e os fontes GLSL de produção permanecem iguais.

O viewer encerra com erro explícito se a suíte GPU falhar. Não carrega o
carro nem repete o mesmo shader após esse resultado. Não existe shader
reduzido, resultado PASS artificial ou teste obrigatório removido nesta
correção. Os limites GL reportados são pistas; o valor constante 16 não
prova sozinho o limite efetivo do compilador GXM/Cg.

## Verificações

`tools/render_link_failure_test.c` exercita GL real: injeta um fragment shader
inválido e simula a resposta otimista de compile status do backend diferido.
O link real falha; exige diagnóstico real não vazio enquanto o shader está
vivo, RProg zero, liberação do shader e estado GL sem erro após rollback.
Rodar em GL e GLES. Também executar os quinze probes existentes em ambos
para verificar o caminho bem-sucedido, com os GLSL de produção preservados.

Comandos, logs e hashes novos pertencem a
`reports/vita-item5-2026-10-06/shader-fix/`, separados da sessão que crashou.
As primeiras tentativas usaram o lock compartilhado, -j1, cap512 MiB,
swap zero e reserva de 4 GiB. Foram recusadas antes de executar por
MemAvailable abaixo de reserva+cap; isso não é falha de código.
O usuário pediu execução limitada sem fechar aplicações. Esta rodada usa
cap384 MiB, -j1, swap zero e reserva de 3 GiB, via cópia privada do guard
em `shader-fix/guarded-validation.py`. O guard histórico fica intacto.
Se o build superar o cap ou o host atingir a reserva, interromper apenas
a validação. Não encerrar processos do usuário.

Status dos testes, archive/VPK novos e sessão física devem ser anexados após
execução. Esta documentação registra o diagnóstico e a alteração preparada;
não afirma que o shader completo já funciona no Vita. Depois de construir e
verificar o pacote novo, instalar uma vez para capturar o diagnóstico de
compilação e retomar o aceite físico do item 5.

## Resultado da preparação e próximo teste físico

Builds dentro de cap384 MiB, reserva3072 MiB e swap0:

| Execução | Pico amostrado | Resultado |
| --- | --- | --- |
| vitaGL rebuild completo | 365,6 MiB | PASS; warnings próprios da receita SDK preservados |
| vitaGL cleanup de log / archive final | 75,8 MiB | PASS |
| Fixture GL/GLES + probes GL/GLES, serializados | 270,4 MiB | Rollback e diagnóstico PASS; 15/15 probes em cada API |
| VPK novo, diretório fresco | 205,3 MiB | 9 unidades C com Werror confirmado, zero avisos |
| Deploy USB com readback | 26,7 MiB | Cinco arquivos conferidos; volume desmontado |

Mesa forneceu GL4.5 Compatibility e GLES3.2 apesar dos pedidos GL2.1/ES2.
Os fixtures verificam o caminho API legacy/GLES, sem alegar implementação
estrita desses mínimos nem equivalência com o console. O patch aplica no
source pinned; objdump confirma branches que impedem consultar GXM com prog
nulo e a liberação de log mesmo em compile fail. Não há execução ARM desse
caminho no host.

Archive corrigido SHA-256:
`beb7b264ccd306621b6cf68c5b2ef729e8105aa30164374bd8787239f14e9057`.
Patch SHA-256:
`09088469872c779635543566c6746dbba812bab10b9cb82f900fde3bd9ede52d`.
O bundle de dependência recuperável está na pasta de evidências dependency.

VPK compilado em `291fcc682ddfe978fad40d16ecc8cdfd2b659c5a`:
1.252.212 B, SHA-256
`bcf534b561374fd9fb54494a64021957c8526e41bca9823cda0205ac384a373c`.
Eboot SHA-256:
`a8076ac393692cd442adef2b0590dbd3d6c79586b76dc5675bbfe2e86bc28b13`.
Sidecar e flags/fontes de build estão em `shader-fix/sdk/`.

Pacote novo preservado:
`/home/luigi/Documents/luigi/nfsu2-data/vita/stock-240sx-shader-fix-2026-10-06/ready/`.
Run ID `5ecc9b7dca8e4984b90e3420`, diferente da sessão do crash.
Transferido para `ux0:data/openug2/car-viewer.vpk` e readback confirmado.
O operador precisa reinstalar no VitaShell e lançar o aplicativo. A
instalação anterior ainda não foi alterada pelo host. Se o shader continuar
sem compilar, o teste deve encerrar com erro explícito e capturar seu log.
Reativar USB para coleta em nova pasta `shader-fix/device-01/`, usando este
bundle novo. Manter `device-01/` anterior como prova do crash original.

Status: `HOST_VALIDATED_DEPLOYED_DEVICE_RETEST_PENDING`. A correção impede o
acesso nulo identificado; o sucesso do shader e a homologação física do item5
continuam pendentes. Não há publicação de issue/PR nesta rodada.
