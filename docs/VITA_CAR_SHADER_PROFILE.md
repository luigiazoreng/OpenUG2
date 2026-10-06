# Shader do viewer estático

Branch `fix-vita-car-shader-profile`, base `integration-development` em
`96eb2b7`. A sessão `2a529edb32d340d4a660a965` confirmou timeout de 123.608 ms
na compilação do fragment shader, mesmo após corrigir os operandos no
tradutor. Log, fontes Cg e hash do executável foram verificados em
`nfsu2-data/reports/vita-item5-2026-10-06/precedence-fix/device-02/`.

## Mudança e escopo

O viewer desativa wetness, reflexos de pista, highlights das luzes do cenário
e projeção/sombras dos faróis sobre o mundo. Mesmo assim, o compilador recebia
todo o código desses efeitos: SSR com 32 passos e refinamento, PCF, oito
luzes e duas projeções de farol. O fragment shader só retorna erro ou sucesso
depois de ser compilado; desligar uniforms durante o draw não reduz essa carga.

`render_program_static_car()` usa o mesmo corpo GLSL do renderer e um define
para excluir esses blocos durante o preprocessing. Preserva texturas,
normals, pintura, rim tint, clearcoat, Fresnel/transparência, emissão dos
faróis, reflexão analítica, fog, piso e legenda. `render_program()` continua
criando o programa completo da engine. O perfil é selecionado apenas pelo
target do viewer e pelos testes que o solicitam.

A variante não habilita cenário, chuva, SSR ou sombras dos faróis no mundo.
Não substitui a homologação do shader completo. O programa completo ainda
tem um bloqueio físico aberto e não é compilado nesta sessão do viewer.

Os parâmetros do compilador voltam ao default da revisão fixada: O3,
fastmath e fastint ativos, fastprecision inativo. O experimento O1 não
resolveu o timeout. Um [relato direto de outro port](https://github.com/robin994/PSVita-Dev-Wiki/blob/main/03-vitagl/05-shaders.md)
também registra hangs após alterar esses knobs; é evidência para conservar
o default, não prova da causa específica do nosso shader.

## Validação e classificação

Os testes do carro real executaram dois ciclos de criar/desenhar/destruir em
cada perfil, para GL e GLES. Foram mantidos 101 meshes, sete texturas, quatro
rodas, 118 draws e payload estimado de 1.860.626 bytes. Câmera, mudança de
pintura, faróis e pausa passaram os critérios visuais do harness.
As dez imagens produzidas ficaram idênticas, pixel a pixel e por hash PNG,
entre o perfil completo e estático nas duas APIs.

As quatro suites de probes passaram 19/19 no host. A variante passou também
o rewriter real da dependência com os seis aliases e compile/link GL/GLES.
A auditoria do fragment preprocessed confirma ausência dos helpers de mundo
e de loops. Nada disso executa SceShaccCg ou homologa o console.

O primeiro probe do viewer chama-se `viewer_program`. Seu log identifica
`GPU SHADER PROFILE static_car full_engine=deferred`. O sidecar declara
`shader_profile=static_car`, e o coletor exige esse perfil e esse probe.
Trocar apenas os nomes ou apresentar logs de outro perfil não pode aprovar
a sessão. Um sucesso gera
`DEVICE_STATIC_CAR_LOG_PASS_MANUAL_REVIEW_REQUIRED`, com a validação da engine
completa explicitamente `DEFERRED`. Todos os probes de pixels, alpha,
DXT nativo/fallback, arrays e mipmaps continuam exigidos; uma falha é fatal.

Os CLI fixtures Python passaram a executar somente sob `__main__`, para
permitir discovery de unittest. Os testes de tooling passaram 17 casos,
com um skip preexistente. Os logs de tentativas e o resultado aprovado ficam
preservados fora do Git.

Builds/testes maiores usam 384 MiB, sem swap, reserva de 3 GiB e lock para
serialização. O archive privado de vitaGL é o já validado na correção de
precedência; não foi recompilado nem alterado nesta entrega. Relatórios:
`nfsu2-data/reports/vita-item5-2026-10-06/car-shader/`.

## Pacote transferido

Build limpo em `5d9a314286b344c8f600f05ab78100962554102f`, dez unidades C,
`-Wall -Wextra -Werror`, zero warnings. Pico: 200,7 MiB, dentro do limite
de 384 MiB. As comparações do carro tiveram pico de 285,5 MiB; probes e
fixture de tradução, 154,4 MiB. Todos passaram sob o guard.

- Archive mantido: `d3411f4d29748509cadab6aff1110b5d46882effdb69e6eccee01c823c061599`
- VPK: `4c33108a2b0a7bdd9562a7629eb3b0e2a77d7ee4003483f2f99fbb973f067073`
- eboot: `cb478e532ac45a44da9c2c9b136dadbec846758c97bf140612ec36e0754d5c1c`
- Sessão: `cee27925449a48f4b9879417`
- Perfil no sidecar: `static_car`; engine completa: `DEFERRED`.

Bundle privado: `nfsu2-data/vita/stock-240sx-car-shader-2026-10-06/ready/`.
O novo VPK e nonce foram transferidos para `ux0:data/openug2/`; os três
assets coincidentes foram preservados. Os cinco arquivos passaram readback
após sync; o USB foi desmontado. Reinstalar `car-viewer.vpk`, abrir a
aplicação e reativar USB no VitaShell para coleta dos resultados.

Estado: **perfil validado no host e transferido; reteste físico pendente**.
Carro dirigível, cenário e shader completo da engine continuam fora da
homologação deste viewer. Não há push, PR ou comentário público desta rodada.
