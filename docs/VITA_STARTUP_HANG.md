# Inicialização bloqueada no link do shader

Branch `fix-vita-startup-hang`, baseada em `integration-development` (`25b7efd`).
O terceiro teste físico instalou o eboot correto e usou a sessão
`e3536f9548bd4049acc17d81`. O log termina em
`RENDER PROGRAM handle=2 phase=link`, sem retorno do link nem início dos
probes seguintes. Evidência privada:
`nfsu2-data/reports/vita-item5-2026-10-06/gl-compat/device-01/`.

SELECT+START era consultado apenas depois de carregar o carro. Enquanto o
link estava bloqueado, o loop de input não executava. O logo animado não
comprova progresso: a biblioteca mantém uma splash thread durante o loading.

## Pesquisa e limites da conclusão

A [documentação oficial do vitaGL](https://github.com/Rinnegatamante/vitaGL/blob/master/README.md)
descreve compilação runtime, flags de diagnóstico e a splash que encobre o
carregamento. O [código da revisão fixada](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/source/custom_shaders.c)
adia a compilação GLSL até `glLinkProgram` e usa por padrão
`SHARK_OPT_FAST`, fastmath e fastint. O [header oficial do vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK/blob/master/source/vitashark.h)
mapeia FAST para O3 e SAFE para O1; seu
[compilador](https://github.com/Rinnegatamante/vitaShaRK/blob/master/source/vitashark.c)
passa essas opções para SceShaccCg.

Isso fundamenta testar O1 sem aritmética relaxada no backend Vita. Ainda não
prova que o otimizador seja a causa do bloqueio: o log anterior não distingue
tradutor, compilador ou registro GXM. Não foi atribuído o problema a módulos
ausentes, plugins do usuário ou limite de uniforms sem evidência.

## Correções e diagnóstico

- O backend configura O1 e desativa fastmath, fastprecision e fastint antes
  dos shaders do aplicativo. O shader de produção completo permanece igual;
  não há retry automático de compilação nem probe suprimido.
- Um monitor nativo independente de SDL/GL lê SELECT+START durante a
  inicialização. Usa stack de 16 KiB e dorme 50 ms entre consultas. Ao entrar
  no loop de renderização, ele termina e o caminho normal de saída volta a
  cuidar dos controles.
- Sem avanço de etapa por 120 segundos, ou após 300 segundos de startup
  total, encerra o processo com erro. Não tenta cancelar uma chamada ativa
  e liberar recursos que o compilador ainda esteja usando.
- `tools/vitagl-startup-trace.patch`, aplicado após os dois patches
  anteriores, acrescenta um callback weak opcional no archive privado.
  Registra tradução, compilação por estágio, registro GXM e link. Sem
  callback, os pontos de trace não fazem IO nem alteram o renderer.
- Os primeiros fontes Cg traduzidos são salvos com limite de 64 KiB e nonce
  da sessão, em `shader-vertex.cg` e `shader-fragment.cg`. O log independente
  é `startup-guard.log`; o coletor conserva esses arquivos e registra se a
  sessão confere. Diagnósticos antigos não comprovam o novo teste.

O monitor e seus retornos/rollback foram exercitados com o código real e
shims determinísticos de kernel sob ASan/UBSan. Isso verifica decisões,
prazos, leitura inválida e lifecycle; não simula o scheduler do console nem
homologa a leitura simultânea ao compiler. O coletor continua exigindo todos
os probes, counts, frames e saída normal; os anexos não substituem a aprovação.

Builds privados mantêm limite de 384 MiB, swap desativada, reserva de 3 GiB
no Linux e `-j1`. O archive instalado no SDK e os pacotes anteriores ficam
preservados. Logs e receitas desta rodada ficam em
`nfsu2-data/reports/vita-item5-2026-10-06/startup-fix/`.

Estado: correções preparadas; comportamento do compilador, resposta do
monitor e renderização do carro ainda precisam de reteste físico.
