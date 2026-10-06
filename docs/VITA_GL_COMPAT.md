# Correções após o segundo teste físico do viewer

Branch: `fix-vita-gl-compatibility`, sobre `integration-development` em
`096afc2`. A base upstream e a matriz histórica de homologação permanecem
fixadas. Este trabalho trata os bloqueios encontrados ao iniciar o viewer
estático do 240SX; a aprovação no host não homologa o console.

## Evidência física anterior

O pacote compilado em `291fcc6` foi instalado corretamente: nonce, identidade
de build e hash do eboot foram conferidos pelo coletor. O log termina com
`CAR SHUTDOWN frames=0 status=1`, após `GPU SUITE passed=10 failed=5`.
Foi uma saída controlada, sem repetir o data abort do primeiro pacote.
O guard do link funcionou e permitiu recuperar o erro do compilador.

Falharam o shader de produção, DXT3/DXT5 nativos e os dois testes de mipmaps.
Os fallbacks RGBA passaram. A coleta privada completa está em
`nfsu2-data/reports/vita-item5-2026-10-06/shader-fix/device-01/`.

## Mudanças

- Shader: renomear a luminância local de `y` para `luma` evita colisão com o
  alias `t -> y` do tradutor. Os cálculos e o shader de produção são mantidos.
  [Detalhes e limites do teste host](VITA_GL_SWIZZLE_COMPAT.md).
- Viewport: calcular as escalas com `0.5f` preserva a área de viewports de
  1×1 e as metades fracionárias de dimensões ímpares.
- Upload manual de mipmaps: copiar o conteúdo enviado ao nível correto,
  preservando os níveis vizinhos. Validar nível, dimensões, formato e tamanho
  de pixel antes de copiar. Entrada permanece compacta, como no upload base;
  não foi adicionado suporte a `GL_UNPACK_ROW_LENGTH`.
- Geração de mipmaps: completar a cadeia até 1×1, incluindo texturas
  retangulares e cadeias parcialmente alocadas. Falha de downscale GXM usa
  o caminho CPU existente de amostragem por decimação.
- Transferências comprimidas 2D: conferir o retorno de GXM e usar o swizzle
  CPU quando falha; níveis menores que um bloco usam diretamente CPU.
  Os blocos DXT permanecem comprimidos. Cubemaps não foram alterados.
- Diagnóstico: acrescentar quatro probes de amostragem RGBA sem blend e
  comparação dos bytes de um bloco comprimido no Vita. Os 15 probes
  anteriores continuam ativos; falhas nativas não são convertidas em sucesso.

Os erros de shader e de viewport/mipmaps têm evidência no código e no log.
A causa específica dos pixels DXT3/DXT5 ainda precisa ser confirmada no Vita:
o retorno ignorado da transferência é um defeito, mas sua correção no host
não prova que ele explica essas duas falhas físicas.

## Dependência e validação

vitaGL está fixado em `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`.
Aplicar, nessa ordem, `tools/vitagl-link-failure.patch` e
`tools/vitagl-texture-compat.patch` em uma cópia limpa. Compilar com
`make -j1 NO_DEBUG=1 SAFE_UNIFORMS=1 HAVE_SHARK_LOG=1 libvitaGL.a` e usar
o archive privado por `VITA_GL_LIBRARY`. A biblioteca instalada no SDK
permanece intacta.

O shader corrigido passou GL/GLES com os seis aliases reais. Os 19 probes
passaram em ambas as APIs no host. Fixtures dos helpers/callsite reais de
mipmaps passaram ASan/UBSan, incluindo falha de transferência e canaries.
O fixture RAW128 verifica dispatch, limites e bloco único; seu swizzler
controlado não comprova a ordenação NEON/Morton real nem execução GXM.

Builds são serializados por `/tmp/openug2-validation.lock`, com cgroup de
384 MiB, swap desativada e reserva de 3 GiB de memória disponível no host.
O guard histórico não foi modificado. Logs, receitas, hashes, fixtures,
archive e pacote ficam fora do Git em
`nfsu2-data/reports/vita-item5-2026-10-06/gl-compat/`.

A homologação física exige nova sessão identificada, suite sem falhas,
carro pronto, frames renderizados e saída normal. Aparência, quatro rodas,
texturas e controles precisam de observação manual. Não há publicação de
issue ou PR nesta etapa.
