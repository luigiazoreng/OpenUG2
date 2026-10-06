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

Estado: perfil validado no host, reteste físico pendente. Carro dirigível,
cenário e shader completo da engine continuam fora da homologação deste viewer.
