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

## Pacote e revisão

Compilado com árvore limpa em `3aee809214edf809299607c79b34bd12b7b8e821`, dez
unidades C sob `-Wall -Wextra -Werror`, sem warnings. Pico da dependência:
353,4 MiB; do aplicativo: 178,0 MiB. Os dois builds passaram. O teste real do
monitor com shims passou ASan/UBSan; os testes de tooling tiveram 16 passes e
um skip preexistente (17 casos). O fixture USB confere também nonce de anexos
atuais e preservação/identificação de fonte Cg antiga.

- Archive privado: `aea53f0247d57cedd5f4429f0ba5ced209119db8c73f4063eb536771262d13f1`
- VPK: `488d0cba8ff30dd62c67f16964e73073d6037e22646a3b764e96d698858b116b`
- eboot: `99d8589679541018d21092eb3c5bfd110d89969ef949a37a43908a36b43f3357`
- Sessão: `4408ecde1e194276aaa5eede`

Bundle privado em
`nfsu2-data/vita/stock-240sx-startup-fix-2026-10-06/ready/`.
O novo VPK e nonce foram transferidos para `ux0:data/openug2/`, preservando
os três assets coincidentes. Os cinco arquivos passaram readback após sync;
o USB foi desmontado. Instalar `car-viewer.vpk`, abrir o aplicativo e depois
reativar USB no VitaShell para coletar os logs e fontes traduzidos.

Estado: **transferido, aguardando reteste físico**. O comportamento do
compilador, a resposta do monitor e a renderização do carro continuam sem
homologação física. Não há push, comentário de issue ou PR desta rodada.
