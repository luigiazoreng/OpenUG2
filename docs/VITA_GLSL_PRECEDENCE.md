# Multiplicação e precedência no tradutor vitaGL

Branch `fix-vita-glsl-precedence`, base `integration-development` em `dab13e4`.
O pacote anterior instalou o eboot correto, com sessão
`4408ecde1e194276aaa5eede`. A tradução GLSL terminou e o vertex shader
compilou/registrou. O fragment shader ficou na chamada de compilação; o
monitor encerrou o processo após 123.553 ms. Portanto O1 não resolveu o
bloqueio. O timeout nativo executou fisicamente; a saída por SELECT+START
durante startup não foi observada nessa coleta.

Evidência física privada:
`nfsu2-data/reports/vita-item5-2026-10-06/startup-fix/device-01/`.
Os dois fontes Cg foram recuperados e seus nonces conferem com a sessão.

## Defeito reproduzido e correção

O [tradutor da revisão fixada](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/source/utils/glsl_utils.c)
reescreve `*` como chamadas de `vglMul`. Ao procurar os operandos, não
reconhecia `&&`/`||` como limites e capturava parte de uma expressão booleana.
O fonte Cg físico contém, por exemplo:

```text
GLSL: if(wet>0.001 || uPaintLights*uClearcoat>0.001)
Cg:   if(wet>vglMul(0.001||uPaintLights,uClearcoat)>0.001)
```

A multiplicação deveria conter somente `uPaintLights` e `uClearcoat`. O
parser alterou tanto os tipos quanto o significado do predicado. Isso também
afeta `wet>0.001 && edge*cutoff>0.0`. A precedência está definida na
[especificação GLSL ES](https://registry.khronos.org/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf).

`tools/vitagl-mul-precedence.patch` acrescenta limites para os operadores
lógicos e completa os limites à direita para igualdade, desigualdade,
ternário e divisão. São oito linhas no tradutor, sem alteração do fonte
GLSL da engine. O caso `a*b/c` preserva a associação `(a*b)/c`.

O teste `tools/vitagl_mul_test.py` extrai a função e os helpers de buffer
reais. Os 17 casos incluem os dois predicados físicos, operadores nos dois
lados, encadeamento, parênteses, indexação, matriz/vetor, números negativos
e notação científica. A versão anterior falha em nove casos; a corrigida
passa todos, com ASan/UBSan limpos.

`tools/render_vitagl_mul_test.py` executa o mesmo parser sobre o fragment
shader completo de `render_program`, injeta os seis aliases do vitaGL e
overloads GLSL de `vglMul`, e usa drivers reais do host. O parser anterior
provoca falha de compile/link em GL e GLES; o corrigido passa nas duas APIs.
Fontes gerados, mensagens dos drivers e logs do guard ficam preservados.
Esse teste valida a reescrita e os tipos no host; não executa SceShaccCg.

O defeito do tradutor é confirmado. Sua relação causal com o timeout do
compilador Vita ainda exige reteste; não há homologação física do shader,
texturas ou carro nesta rodada.

## Dependência, economia de build e coleta

Aplicar os patches na ordem: `vitagl-link-failure.patch`,
`vitagl-texture-compat.patch`, `vitagl-startup-trace.patch`,
`vitagl-mul-precedence.patch`, sobre vitaGL
`2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`.
O archive instalado no SDK permanece intacto.

Para esta entrega, somente `glsl_utils.o` precisa de recompilação. Todos os
outros fontes compiláveis, o Makefile, a revisão e os flags foram conferidos
contra o archive anterior. Uma cópia privada desse archive recebe o novo
objeto; os demais membros são comparados por hash. O build completo também
pode ser reproduzido pelos quatro patches e pela receita já documentada.

Mantidos O1, monitor nativo, traces, dumps Cg, todos os probes e o viewer
original. Builds e testes maiores usam lock, `-j1`, limite de 384 MiB,
swap desativada e reserva de 3 GiB para o Linux. Relatórios privados:
`nfsu2-data/reports/vita-item5-2026-10-06/precedence-fix/`.

## Pacote transferido

Build limpo em `1e3dc9f2b8f6c91cc8c34a59449fe4dc7d6b538a`, dez unidades C
sob `-Wall -Wextra -Werror`, sem warnings. Rebuild do tradutor: pico 78,8 MiB;
build do aplicativo: 184,5 MiB. Ambos passaram sob limite de 384 MiB. Auditoria
do archive comprova que somente `glsl_utils.o` mudou. Os quatro patches
aplicaram sobre a revisão limpa e reproduziram os fontes modificados.

- Archive: `d3411f4d29748509cadab6aff1110b5d46882effdb69e6eccee01c823c061599`
- VPK: `22b5127dcaaa7d3a70f5fc0d857de7734c783f831c7486201877ade006497c7a`
- eboot: `3cfa797b8f8396d6b7eef30fa4a28016b69137d5173da006390711ec49546f41`
- Sessão: `d1ac8c2b09564608921a5782`

Bundle privado:
`nfsu2-data/vita/stock-240sx-precedence-fix-2026-10-06/ready/`.
VPK e nonce foram transferidos para `ux0:data/openug2/`; os três assets
coincidentes foram preservados. Os cinco arquivos passaram readback após
sync; o USB foi desmontado. Reinstalar `car-viewer.vpk`, abrir a aplicação e
reativar USB no VitaShell para coleta da nova sessão.

Estado: **correção reproduzida no host e transferida; reteste físico pendente**.
Sem push, PR ou comentário público. A matriz histórica de homologação está
preservada e não aprova esta entrega automaticamente.
