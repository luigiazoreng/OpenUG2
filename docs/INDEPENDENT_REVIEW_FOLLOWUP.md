# Tratamento do parecer independente

O parecer fornecido pelo usuário mantém `APPROVED_HOST_ONLY` para a entrega
6414e1a/addbcbe. As evidências e a tag dessa entrega permanecem preservadas.
Os dois achados TPK devem ser corrigidos antes de afirmar que os limites do
decoder foram validados e antes de usar esse decoder no aceite do console.
Veja `TPK_REVIEW_FIXES.md` para as reproduções independentes.

O trabalho Vita posterior ao handoff original está isolado em
`vita-device-tests` / `620f2d9`, com `VITA_DEVICE_HANDOFF.md` e evidências novas.
Ele não foi incluído na revisão host do parecer. Seus testes físicos cobrem
somente o bring-up, parser stock, pausa/retry/saída; não atestam a engine
completa ou a cobertura dos parsers de textura. A execução até o carro real
foi autorizada após esse parecer, mantendo uma branch por issue/slice.

## Pontos informativos tratados

- O diagnóstico `--event-refs` agora rejeita stems que não cabem nos sete
  caracteres do seu modelo, antes de procurar arquivos. Não publica um stem
  truncado como se fosse o caminho solicitado. Os stems reais têm quatro
  caracteres; esse limite não altera o carregamento normal ou as capturas.
- O comentário em `world_bind_key` foi atualizado: `n2_tpk_decode` define a
  saída; não deixa `dxt` intocado.
- `machine-id` é uma barreira contra relabelar a mesma identidade, não prova
  hardware independente. VM/container no mesmo computador pode fornecer
  outro ID. A aceitação da #7 exige identificação/proveniência dos dois hosts
  físicos pelo operador, além do comparador. O Vita bootstrap não substitui
  a matriz da engine da segunda máquina.
- O runner histórico `compare-preserved-reference.py` depende explicitamente
  de `/home/luigi/Documents/luigi/OpenUG2-issue7/tools/stream_coverage.py` e do
  checkout da #7 em `7e95be5bd4193f95724c3b7165ea40d4f8d5bc79`. Essa dependência
  não foi incorporada à engine. Mantenha esse worktree para reproduzir a
  rodada histórica; novos runners devem declarar sua dependência/revisão.
- A margem de tráfego medida na revisão (tangencial máximo aproximadamente
  2,5e-7 para tolerância 1e-5) corrobora a correção. Captura de um frame não
  exercita essa física. Custo de double/aritmética newlib no Vita permanece
  pendente; não é critério de aprovação do car viewer estático.

## Validação de follow-up em 2026-10-05

O fixture `tools/resource_test.c` agora aceita `boundary` ou `prefix`; sem
argumento, preserva os dois casos originais. O prefixo foi executado
isoladamente contra `resource.c` de `6c9dd091ee3297dea898c78764edf209657ce3d5`.
Com um stem de 250 caracteres e uma pasta existente no prefixo truncado, o
ASan acusa `allocation-size-too-big` em `n2_read_file`: o código antigo abre o
diretório pelo nome truncado e tenta tratá-lo como arquivo. O mesmo caso passa
com o `src/resource.c` atual, e o modo sem argumentos passa ambos os testes.
O log guarda o diagnóstico ASan completo. A macro POSIX do fixture declara
`mkdtemp` corretamente.

O binário desktop normal foi compilado com `-Werror` e SDL2 via os headers e a
biblioteca staged já usados neste worktree. Em uma árvore artificial com
`TRACKS/STREAMABCDEFGH.BUN`, `--event-refs` imprime que o stem de oito
caracteres excede sete e encerra com código 2 antes de uma busca truncada.

Sob o guard de 512 MiB e swap zero, `car-material-test` passou 361 verificações,
`world-texture-test` passou, e os casos `header` e `metadata` de
`tpk_review_test` passaram separadamente com ASan/UBSan. O maior pico amostrado
foi 457,7 MiB durante o build normal `-Werror`; o menor valor de memória
disponível no host foi 6160,3 MiB, acima da reserva de 4 GiB.

Comandos, stdout/stderr, JSON do guard e SHA-256 estão em
`/home/luigi/Documents/luigi/nfsu2-data/reports/independent-review-followup-2026-10-05/`.
Nenhum arquivo dos relatórios selados anteriores foi alterado.

As reproduções sanitizadas do fixture de recursos, cada uma em um processo e
guard separados, foram:

```sh
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Wno-format-truncation -Isrc tools/resource_test.c /tmp/openug2-development-6c9dd09/src/resource.c -lm -o /tmp/resource-review-baseline && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/resource-review-baseline prefix'
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Isrc tools/resource_test.c src/resource.c -lm -o /tmp/resource-review-fixed && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/resource-review-fixed prefix'
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/resource-review-fixed'
```

O build normal e o caso `--event-refs` foram executados juntos sob o guard:

```sh
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'make -j1 normal CFLAGS="-O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function" SDL_CFLAGS="-I/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2 -I/tmp/openug2-vita-reference/desktop-sdl/usr/include/x86_64-linux-gnu -D_REENTRANT" SDL_LIBS="-l:libSDL2-2.0.so.0" && OPENUG2_EVENT_REFS_FIXTURE=$(mktemp -d /tmp/openug2-event-refs-XXXXXX) && mkdir -p "$OPENUG2_EVENT_REFS_FIXTURE/TRACKS" && : > "$OPENUG2_EVENT_REFS_FIXTURE/TRACKS/STREAMABCDEFGH.BUN" && set +e; ./nfsu2 --event-refs "$OPENUG2_EVENT_REFS_FIXTURE"; OPENUG2_EVENT_REFS_STATUS=$?; set -e; printf "event-refs exit=%s\n" "$OPENUG2_EVENT_REFS_STATUS"; rm -rf "$OPENUG2_EVENT_REFS_FIXTURE"; test "$OPENUG2_EVENT_REFS_STATUS" -eq 2'
```

Os demais comandos guardados foram `make -j1 car-material-test`,
`xvfb-run -a make -j1 world-texture-test` com os overrides SDL do log, e a
compilação de `tools/tpk_review_test.c` com `-fsanitize=address,undefined`
seguida separadamente por `header` e `metadata`. Consulte `sha256.txt` para os
hashes dos arquivos-fonte, binários e logs.

A compilação do VPK em diretório novo e a verificação independente de
CRC/conteúdo/title/SHA continuam pendentes para a etapa seguinte. Os testes
desktop desta rodada não cobrem o renderer ou hardware Vita.
