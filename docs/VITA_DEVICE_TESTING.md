# Linux ↔ PS Vita: transferência e testes

Esta entrega está na branch `vita-device-tests`, no worktree
`/home/luigi/Documents/luigi/OpenUG2-vita-device-tests`. A base homologada
`integration-development` / `6414e1a` continua preservada para revisão.
Não há alteração no executável da engine nem nos plugins do console.

## Conexão inicial

O Vita precisa estar desbloqueado, com **VitaShell** funcionando. Só conectar
o cabo não inicia a exposição do cartão ao Linux. No VitaShell, pressione
**START** para configurar `SELECT button` como `USB` ou `FTP`, depois **SELECT**
para iniciar a conexão. Esses modos são definidos nas
[configurações do VitaShell](https://github.com/TheOfficialFloW/VitaShell/blob/master/settings.c).

**USB:** escolha em `USB device` o armazenamento que corresponde ao `ux0:`
ativo, por exemplo o cartão de memória ou SD2Vita. Não escolha o cartucho.
Abra o volume no gerenciador de arquivos do Linux; a raiz deve conter `app/`.
O utilitário exige um volume montado e não monta, formata ou altera permissões
do sistema. Se aparecer um disco sem montagem, identifique sua partição por
`lsblk -e 7 -o NAME,TRAN,SIZE,FSTYPE,LABEL,MOUNTPOINTS` e monte pelo gerenciador
de arquivos. Se não aparecer no `lsusb`, mantenha o VitaShell em USB e confira
porta e cabo com capacidade de dados. Não instale CMA/QCMA para este fluxo de
armazenamento do VitaShell.

**FTP:** conecte Linux e Vita à mesma rede Wi-Fi, mantenha a tela de conexão
do VitaShell aberta e use o endereço/porta mostrado. O Linux desta sessão
usa `192.168.68.103`; o IP do Vita precisa ser informado pelo console.
O cliente usa FTP passivo, timeout de 15 segundos e os caminhos `/ux0:/...`,
também usados pelo [build do próprio VitaShell](https://github.com/TheOfficialFloW/VitaShell/blob/master/CMakeLists.txt).
Usa `CWD` + `LIST`, compatíveis com o
[libftpvita](https://github.com/xerpi/libftpvita/blob/master/libftpvita/ftpvita.c),
sem depender de `NLST`, `MLSD` ou `SIZE`. Uma falha de listagem/permissão
interrompe a operação; não é interpretada como autorização para sobrescrever.

Diagnóstico somente de leitura, sem varredura de rede:

```bash
cd /home/luigi/Documents/luigi/OpenUG2-vita-device-tests
python3 tools/vita_device.py doctor
python3 tools/vita_device.py doctor --usb-root /media/luigi/NOME_DO_VOLUME
# Alternativa: substituir pelo IP exibido no VitaShell.
python3 tools/vita_device.py doctor --host IP_DO_VITA --port 1337
```

Antes de executar o VPK, confirme que o console possui o compilador de shaders
`libshacccg.suprx` extraído/descriptografado, requisito documentado pelo
[vitaGL](https://github.com/Rinnegatamante/vitaGL#prerequisites).
Não distribuímos essa biblioteca no VPK.

## Transferir o teste pequeno

O teste inicial envia apenas **8.42 MiB**: VPK de 1.148.702 bytes e geometria
240SX de 7.675.600 bytes. Não copie as ISOs nem o mapa inteiro.
O VPK existente no worktree principal já foi compilado/homologado no host:

```text
title ID: OPENUG200
VPK SHA-256: ff15e26003adafdacf44f93852bb205a03f0eefcd1f035c322ee996d8911ea3b
source: addbcbe2a8d3ec6911004fad418443f96c9ebe1f
```

```bash
python3 tools/vita_device.py deploy \
  --usb-root /media/luigi/NOME_DO_VOLUME \
  --vpk /home/luigi/Documents/luigi/OpenUG2/build/vita/openug2-vita.vpk \
  --geometry /home/luigi/Documents/luigi/nfsu2-data/game/CARS/240SX/GEOMETRY.BIN \
  --reports /home/luigi/Documents/luigi/nfsu2-data/reports/vita-device
```

Para FTP, substitua `--usb-root ...` por `--host IP_DO_VITA --port 1337`.
Não há dependências Python externas para operar o utilitário.

O comando imprime `RUN=...` e o caminho do VPK no console. Ele:

1. Verifica CRC, conteúdo restrito do VPK e title ID antes de transferir.
2. Arquiva o log anterior no Linux, caso exista.
3. Coloca a geometria em `ux0:data/openug2/CARS/240SX/GEOMETRY.BIN`. Um
   arquivo existente idêntico é reutilizado; um diferente interrompe a operação
   e é preservado. Não há opção de sobrescrita.
4. Publica o VPK em `ux0:data/openug2/deploy/<run_id>/openug2-vita.vpk`.
5. Relê os dois arquivos do dispositivo, confere SHA-256 e grava
   `deployment.json` no diretório exclusivo daquela execução.

As cópias são feitas em blocos de **64 KiB**, com limite de 32 MiB por arquivo,
2 MiB por log e 4.096 entradas por listagem FTP. Uma transferência interrompida
pode deixar um arquivo `.partial-*` no console; ele não é um VPK instalado.
O utilitário não instala pacotes, altera `tai/config.txt` nem escreve em `app/`
ou `ur0:`. Execute uma transferência por vez, com espaço livre suficiente
para o VPK, geometria e instalação (reserve pelo menos 32 MiB no `ux0:`).

## Instalar e executar no console

1. No USB, espere `deploy` terminar, ejete/desmonte o volume pelo gerenciador
   de arquivos do Linux e encerre a conexão no VitaShell. No FTP, espere o
   comando terminar e encerre o servidor pelo VitaShell.
2. No VitaShell, navegue ao caminho do VPK que o comando imprimiu e instale-o.
   O title ID deve ser `OPENUG200`; não selecione outro pacote. Se esse title
   já existir, trata-se da atualização deste aplicativo de teste.
3. Abra **OpenUG2 Vita Bring-up** pela LiveArea. Observe triângulo texturizado
   branco/laranja e fundo verde, correspondente ao parser do 240SX. O carro
   ainda não é desenhado. Se houver erro ou fundo vermelho, registre isso.
4. Mantenha-o aberto por aproximadamente 30 segundos. Teste analógico esquerdo,
   L/R, Cross, Square, Triangle e START. START alterna pausa/cor; Cross refaz
   o parser; Triangle registra a ação de câmera. Para valores de botões
   aparecerem no perfil, segure cada um por cerca de dois segundos.
5. Saia com **SELECT+START**. Reabra VitaShell e restabeleça a conexão.

VitaShell USB/FTP é o canal de transferência. Ao abrir o aplicativo de teste,
esse canal normalmente deixa de estar disponível; reconectar depois da saída
faz parte do procedimento. A LiveArea não fornece os argumentos `--frames`
e `--parser-only` deste executável: este fluxo usa abertura/saída manuais.

## Recolher e avaliar a evidência

Use o caminho `RUN` impresso pelo deploy. Primeiro, é possível coletar sem
confirmar observações: nesse caso o resultado fica `REVIEW_REQUIRED`.
Depois de observar o teste, informe somente o que realmente foi verificado:

```bash
python3 tools/vita_device.py collect \
  --usb-root /media/luigi/NOME_DO_VOLUME \
  --run /home/luigi/Documents/luigi/nfsu2-data/reports/vita-device/vita-RUN_ID \
  --installed-vpk yes --triangle yes --green yes --inputs yes \
  --device-info 'PCH-XXXX; firmware X.XX; VitaShell X.XX'
```

FTP funciona com a mesma substituição de transporte. Cada coleta cria um
subdiretório novo, preserva o log bruto, relê os artefatos enviados e produz
`result.json`. Códigos de saída: `0` evidência aceita para o smoke test; `2`
erro de conexão/arquivo/transferência; `3` revisão necessária. Se o comando
interromper, o diretório incompleto permanece visível para diagnóstico.

Para aceitar o smoke test, a coleta exige: log diferente do anterior, cabeçalho
do bootstrap, drawable 960×544, recursos GL inicializados, perfil CPU/pools,
saída com frames > 0 e status 0, ausência de erros reportados e contagens
240SX **101 meshes / 30.571 vértices / 18.237 índices / 24 hashes**. Também
exige identificação manual do console e confirmação da instalação, imagem
e controles. Hash diferente sozinho não comprova execução nova; essas
confirmações permanecem parte obrigatória da evidência. O VPK verificado no
armazenamento também não prova por si só qual aplicativo foi instalado.

Faça um novo `deploy` e repita o ciclo três vezes para testar saída/reabertura.
Arquive cada execução; `vita-bringup.log` é truncado pelo aplicativo a cada
abertura. Não abra novamente antes da coleta. Falhas de renderização/parser,
crash e ausência de saída devem ser registrados, sem marcar a homologação
como concluída. Não mova ou remova dados existentes para simular ausência
nesta primeira sessão; os casos negativos sintéticos já são testados no host.

Escopo: **V0**, parte inicial de **V1** (triângulo RGBA indexado) e **V2**
(parser stock). Shaders completos, DXT, FBO, carro/mapa real, física e tráfego
ainda dependem das entregas seguintes. Pools livres não medem pico de VRAM
nem RAM total do processo. O Vita não substitui automaticamente o segundo
computador físico exigido pela matriz host da issue #7.

## Automação posterior e revisão

Depois do primeiro teste físico, podemos preparar uma etapa separada para
execução remota. O [Vita Companion](https://github.com/devnoname120/vitacompanion)
oferece FTP em segundo plano e comandos de abrir/fechar aplicativo, mas requer
plugins taiHEN e configuração/reboot do console. Não é requisito desta entrega,
e nenhum plugin foi instalado. O suporte depende da versão realmente presente
no aparelho; não envie comandos de reboot ou encerramento global para testar.

Validação deste utilitário:

```bash
python3 tools/vita_device_test.py -v
# Opcional: teste de integração FTP com servidor real em localhost.
python3 -m venv /tmp/openug2-vita-device-validation
/tmp/openug2-vita-device-validation/bin/pip install pyftpdlib==2.2.0
/tmp/openug2-vita-device-validation/bin/python tools/vita_device_test.py -v
```

O teste USB usa um volume simulado; o teste FTP opcional usa localhost.
Esses testes automatizados não validam USB/FTP do console físico. Os fixtures de VPK, geometria
e logs são sintéticos e não executam código no Vita. A evidência local desta
entrega fica em `../nfsu2-data/reports/vita-device-tooling-2026-10-05/`, fora dos
relatórios selados da homologação host anterior. Não houve publicação de PR
ou comentário em issue.

### Primeira conexão física — 2026-10-05

Após ativar VitaShell USB, o console apareceu como `054c:05d3`, PS Vita Type A,
interface mass storage com driver `usb-storage`. O Linux montou `/dev/sda`,
exFAT, em `/media/luigi/2CDA-8AFC`; a configuração do VitaShell identificou
`USBDEVICE=2` (SD2Vita). Havia aproximadamente 18 GiB livres.

A transferência **real pelo USB** do VPK homologado e da geometria 240SX
foi concluída, relida do dispositivo e verificada por SHA-256. Não existiam
dados OpenUG2 nesses destinos. O volume foi desmontado com `udisksctl unmount`
antes da instalação manual. Pico amostrado no host: **20,4 MiB**, com limite
512 MiB e mínimo de aproximadamente 7,4 GiB disponíveis no computador.

Evidência desta transferência:
`../nfsu2-data/reports/vita-device/vita-20261006T005820Z-c9e65762/` contém
`deployment.json`, `linux-usb.json`, `transfer-validation.json` e as duas
releituras. VPK no Vita:
`ux0:data/openug2/deploy/20261006T005820Z-c9e65762/openug2-vita.vpk`.

Na primeira execução, o usuário confirmou o triângulo branco/laranja e fundo
verde. O log foi recolhido em `collection-ef9b0f29/vita-bringup.log`, dentro do
diretório acima, e comprova:

- drawable **960×544** e recursos GLSL/VBO/IBO/RGBA inicializados;
- parser stock **101 meshes / 30.571 vértices / 18.237 índices / 24 hashes**,
  carregamento **2.548,153 ms**, `mesh_cpu_bytes=1028482`;
- saída limpa após **530 frames**, status **0**;
- 17 amostras após o primeiro frame, **30,54–30,66 FPS**;
- pools livres constantes nas 18 amostras, sem medição de pico de VRAM/RAM.

O primeiro frame teve **1.012,104 ms**; sua causa não foi determinada.
Essa amostra está preservada, separada das medidas seguintes. A mediana de
CPU submit nas amostras seguintes foi **0,349 ms** e de frame **32,712 ms**.

A coleta original marcou falso erro porque o verificador interpretava
`GPU ms/process RAM unavailable` (mensagem informativa) como falha. A correção
passou nos seis testes, incluindo diagnósticos reais de falha e essas mensagens
informativas. `reassessment.json` reavalia o mesmo log sem alterar o original;
as oito checagens automáticas passaram. `runtime-summary.json` registra os
resultados e limites. **O smoke test completo permanece pendente**, pois o
usuário informou que ainda não testou todos os controles. Também faltam
identificação modelo/firmware e ciclos adicionais de saída/reabertura.

Primeira evidência física: V0 instalado/contexto/saída, parte inicial de V1
(triângulo RGBA) e V2 (parser stock). FTP físico, demais probes V1 e renderer
completo continuam pendentes. Nenhum comentário foi publicado em issue.

Para o teste dos controles, foi preparado no host
`../nfsu2-data/reports/vita-device/vita-controls-a7476a78/deployment.json`.
Ele reutiliza os mesmos arquivos previamente verificados e associa como
baseline o hash do primeiro log; não houve nova transferência ou mudança de
VPK. A próxima coleta nesse diretório exigirá um log diferente e repetirá a
conferência dos artefatos armazenados no Vita.

### Segunda coleta — controles parcialmente observados

`vita-controls-a7476a78/collection-cf206d99/` preserva o novo log e
`input-summary.json`. Há evidência de analógico esquerdo (`steer` até 0,77),
L (`brake=1`) e Cross (`nitro_confirm=1`, duas novas leituras do parser).
Não há amostras ativas de R ou Square, nem mensagens de pausa/ação Triangle.
Isso não comprova falha de mapeamento: os estados mantidos são amostrados
uma vez por segundo e podem perder pressões curtas.

O usuário informou não perceber mudança ao pressionar botões/analógico.
Esses controles não movimentam o triângulo neste bootstrap. Cross apenas
refaz o parser; L/R/Square/analógico alimentam o log e Triangle registra
a ação sem câmera. **START deve mudar a área branca do triângulo para um
tom amarelado**, alternando a componente azul entre 1 e 0,3; pressione-o
novamente para retornar. Esse efeito deve ser verificado separadamente.

Também falta `shutdown complete` no segundo log. Não foi estabelecida a
causa (por exemplo, troca de aplicativo, encerramento externo ou dificuldade
com a combinação). O caso permanece pendente até testar SELECT+START e
recolher a saída limpa. A primeira saída limpa já registrada é preservada.
