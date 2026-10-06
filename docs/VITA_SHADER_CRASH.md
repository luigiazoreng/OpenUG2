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
Builds/testes usam o lock compartilhado, -j1, cap512 MiB e swap zero,
preservando 4 GiB no host. As primeiras tentativas foram recusadas antes de
executar por MemAvailable abaixo de reserva+cap; isso não é falha de código.
Não baixar a reserva para contornar a recusa nem encerrar processos do usuário.

Status dos testes, archive/VPK novos e sessão física devem ser anexados após
execução. Esta documentação registra o diagnóstico e a alteração preparada;
não afirma que o shader completo já funciona no Vita. Depois de construir e
verificar o pacote novo, instalar uma vez para capturar o diagnóstico de
compilação e retomar o aceite físico do item 5.
