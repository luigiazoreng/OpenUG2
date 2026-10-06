# Diagnóstico de mipmaps e raster mínimo

Branch `diagnose-vita-mipmap-sampling`, sobre `integration-development`
(`eed8928`). A sessão `cee27925449a48f4b9879417` foi instalada corretamente e
terminou com `CAR SHUTDOWN frames=0 status=1`: saída controlada, antes de
carregar o carro. O shader estático compilou/registrou, e DXT1/3/5 nativos,
incluindo alpha e pixels sem blend, passaram fisicamente.

Resultado: 17 PASS, duas falhas (`mip_sampling`, `mip_generation`), em
5.821 ms. Ambas leram o azul do clear. O log não distingue um draw ausente
de uma amostra azul. A inspeção dos dumps encontrou como arquivos mais
recentes os dumps já coletados na primeira falha de link; não há evidência
de um novo data abort nesta sessão.

Coleta privada:
`nfsu2-data/reports/vita-item5-2026-10-06/car-shader/device-01/`.

## Nova coleta preparada

Esta entrega acrescenta diagnóstico, sem afirmar uma nova correção do
backend de mipmaps. A biblioteca e os critérios anteriores permanecem.

- Os dois checks originais continuam usando viewport de 1×1, UV de 0 a 1 e
  as mesmas cores esperadas; suas falhas continuam fatais.
- Dois checks usam viewport de 8×8 e UV multiplicada por oito. Uma textura
  de 16×16 mantém o mesmo gradiente de 16 texels por pixel, ou LOD4. Isso
  separa minificação de rasterização em 1×1.
- Dois checks de cor sólida usam uma textura base completa e min filter
  `GL_NEAREST`, em viewports de 1×1 e 8×8. Assim não dependem de mipmapping.
- A inspeção privada do Vita mostra descriptor, contagem e conteúdo dos
  cinco níveis gerados e fornecidos manualmente. Lê apenas o formato RGBA8,
  base 16×16 e count cinco previstos pelo helper pinado. Mismatch é registrado
  como inspeção omitida. Bytes não substituem os checks de pixel.
- A espera de transferências para inspeção CPU ocorre depois dos checks de
  pixel, sem reclassificar os resultados anteriores. A cadeia manual usada
  pela inspeção final é reconstruída independentemente da geração.

As quatro suites host (GL/GLES, programa completo/estático) passaram
23/23. Pico sob guard: 248,9 MiB. Os novos checks precisam de execução
física; resultado host não prova funcionamento GXM. O sampler do shader
sintético ganhou somente um uniform de escala UV, inicializado a um em
todos os draws anteriores.

Relatórios privados:
`nfsu2-data/reports/vita-item5-2026-10-06/mip-diagnostics/`.
O mesmo archive privado de vitaGL é usado. Sem publicação de issue ou PR.
Estado: diagnóstico host validado; reteste físico pendente. O carro e o
shader completo do cenário continuam sem homologação física.

## Empacotamento do SDK

O primeiro ELF compilou, mas `vita-elf-create` pediu 4.176 bytes para
metadados SCE; a folga entre os segmentos era de apenas 3.656 bytes.
O linker `armvita` deste SDK oferece `__sce_headroom`: o target agora
reserva 8 KiB antes de alinhar o segmento gravável a 64 KiB. Isso ajusta
o layout, sem aumentar o limite de RAM do build ou mudar a biblioteca.
O diagnóstico original e seu log de falha são preservados no relatório
privado. O [empacotador oficial](https://github.com/vitasdk/vita-toolchain/blob/master/src/vita-elf-create/sce-elf.c)
verifica essa sobreposição antes de acrescentar os metadados ao segmento.
