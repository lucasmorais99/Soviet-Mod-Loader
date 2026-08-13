# Guia do mantenedor

Este documento descreve como manter, validar e publicar o Soviet Mod Loader.
Ele é destinado ao responsável pelo projeto, não ao usuário final.

## Modelo de distribuição

O TesmioLoader é o host: descobre DLLs, oferece configuração, log, serviços e
VFS. O pacote atual do loader não precisa trazer plugins de conteúdo. O SML é
um único plugin opinativo que centraliza quatro capacidades:

| Capacidade | Fonte incorporada | Responsabilidade no SML |
|---|---|---|
| buildings | `vendor/tesmio/buildings.cpp` | materializar definições de construções |
| resources | `vendor/tesmio/resources.cpp` | ampliar o catálogo de recursos |
| deposits | `vendor/tesmio/deposits.cpp` | tipos, canais DDS e persistência por partida |
| needs | `vendor/tesmio/needs.cpp` | ampliar necessidades e instalar hooks dependentes |

As quatro fontes são unidades de compilação separadas, com entry points
renomeados e não exportados. A DLL final exporta somente a API do SML. Isso
preserva o estado interno de cada componente e permite ao SML executar todos
os `Init` depois de descobrir, validar, confirmar e aplicar os mods.

O `buildings` standalone foi estacionado no build upstream b0.3.6. Ele é
mantido aqui deliberadamente: não contém patches por endereço no executável e
continua necessário para o catálogo integrado e para a proteção de
`workshop_wip`.

## Compatibilidade atual

- TesmioLoader: b0.3.6.
- API do host: 4; hooks filhos podem declarar intervalo 3–4.
- Jogo: v1.1.1.9.
- Revisão upstream sincronizada: `3baa141f9f08921aea9c95f0a400289cabd9960a`.
- Nome do plugin: `soviet_mod_loader.dll`, sem o prefixo histórico `000_`.

O host API 4 acrescenta `TsmHost::vfsRoot`. O SML usa esse caminho quando o
campo está disponível e mantém o resolvedor anterior como fallback defensivo.
Os componentes que aplicam hooks conservam as verificações de versão e
prólogo do upstream; uma versão desconhecida do jogo deve falhar suavemente.

Consulte o [changelog upstream](https://github.com/MaxLegend/TesmioLoader/blob/master/changelog.md),
a [arquitetura do host](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/01-architecture.md)
e o [cabeçalho da API](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h)
antes de cada atualização.

## Fluxo de inicialização

1. Descobrir mods da Workshop e ler `soviet.mod.ini`.
2. Validar versão, dependências e hooks; ordenar os mods.
3. Planejar merges, catálogo, conflitos e assets integralmente em memória.
4. Aplicar os invariantes operacionais e validar referências cruzadas sem
   escrever no disco.
5. Bloquear pastas inconsistentes no intervalo `91XXXXXXXX` de
   `media_soviet/workshop_wip`.
6. Exibir a confirmação quando a política exigir.
7. Aplicar snapshots, catálogos, INIs e assets somente após aceite.
8. Desabilitar os plugins externos equivalentes e executar `resources`,
   `deposits`, `needs` e `buildings`, nessa ordem de dependência.
9. Comparar catálogos planejados e reais, patches de deposits e resultado de
   buildings; persistir `confirmation.index` somente se tudo passar.
10. Executar `Init` dos hooks dos mods; na fase `Start`, iniciar componentes e
   hooks dependentes.

Recusa, fechamento da janela ou falha de interface encerram o processo antes
da aplicação. `confirmation_mode = never` é a opção explícita para execução
sem interface.

## Invariantes e bloqueios da versão 0.5.2

O merge final força capacidades exigidas pelo conteúdo, mesmo que um baseline
antigo ou `allow_settings` tente desligá-las:

- resources não vazio: `hook = 2`;
- deposits não vazio: `code_patch = 1`;
- needs não vazio: `enabled = demand = storage = 1`;
- buildings não vazio: `enabled = 1` e
  `out = media_soviet\workshop_wip`.

Opções de diagnóstico, preços, balanceamento e interface permanecem
configuráveis. Depois de cada `Init`, funções internas retornam contagens e
estado dos hooks ao gerenciador; elas não fazem parte de `SmlApi`. Qualquer
diferença entre plano e runtime, patch obrigatório recusado ou building
`INCOMPLETE` bloqueia a partida. `needs` termina sua validação na fase `Start`,
quando seus hooks dependentes são instalados.

O gerador universal aceita `richness_offset` por depósito no intervalo
`[-0.25, +0.25]`. Ele é aplicado ao campo fractal antes do limiar fixo `0.61`,
portanto afeta cobertura e intensidade. O padrão é zero e mantém o checksum da
0.5.1. A chave nunca invalida uma entrada já inicializada no manifesto: uma
mudança de balanceamento vale somente para canais criados posteriormente.

## Atualização a partir do upstream

Para cada nova versão do TesmioLoader:

1. Registre o SHA e leia `changelog.md` por completo.
2. Compare `src/tesmio_api.h` e `src/tesmio_plugin.h`; sincronize os cabeçalhos
   antes dos componentes.
3. Compare individualmente `resources`, `needs`, `deposits` e `buildings`.
4. Reaplique apenas as diferenças intencionais do SML:
   - include local `tesmio_plugin.h`;
   - entry points internos sem `dllexport`;
   - geração universal e manifesto de depósitos;
   - inicialização controlada pelo gerenciador.
5. Revise todos os RVAs, prólogos e gates de versão presentes nas fontes
   incorporadas. Nunca copie endereços entre versões por suposição.
6. Atualize a matriz acima, `NOTICE.md`, os três guias e a versão do SML.
7. Execute o checklist de validação e teste no jogo em um perfil descartável.

`resources.cpp` e `needs.cpp` devem permanecer tão próximos quanto possível do
upstream. `deposits.cpp` é um port do componente atual acrescido da geração e
persistência do SML. Mudanças no `buildings` exigem revisão manual porque o
componente não faz mais parte do build ativo upstream.

## Build e testes

Requisitos: Windows e Visual Studio Build Tools com o toolset C++ x64. Na raiz:

```bat
test.bat
build.bat
```

O resultado distribuível fica em:

```text
build/plugins/soviet_mod_loader.dll
build/plugins/soviet_mod_loader.ini
```

O build usa C++17 e runtime estático (`/MT`). O script remove artefatos antigos
com prefixo `000_` antes de gerar o pacote.

Checklist mínimo de release:

- testes automatizados aprovados;
- build Release aprovado e somente três exports Tesmio na DLL;
- nenhuma referência ativa a `000_soviet_mod_loader`, salvo migração/limpeza;
- teste de aceite e recusa da confirmação;
- teste do bloqueio `workshop_wip` com uma pasta inconsistente;
- mundo novo com depósitos e reload após salvar;
- baseline `resources.hook=1` normalizado para `2` quando `[list]` não está vazio;
- referência inexistente bloqueada antes de aplicação e building incompleto
  bloqueado depois da geração;
- `saved_last`, `campaign1`, `save\<mundo>`, traversal e caminho absoluto fora
  de `media_soviet` exercitados;
- checksum com offset zero compatível com 0.5.1, monotonicidade para offsets
  positivos/negativos e rejeição dos valores fora de `[-0.25, +0.25]`;
- mod incompatível isolado sem impedir mods válidos;
- instalação limpa e atualização sobre uma versão antiga;
- `README`, três guias, versão e SHA upstream atualizados.

## Publicação e migração do nome

Distribua a DLL, o INI, a documentação e o template. Não distribua os quatro
plugins standalone. Nas notas de atualização, instrua a remoção manual de:

```text
tesmioloader/build/plugins/000_soviet_mod_loader.dll
tesmioloader/build/plugins/000_soviet_mod_loader.ini
```

O SML aceita o INI antigo como fallback quando o novo não existe, mas essa é
uma ponte de migração, não uma configuração permanente. Duas DLLs simultâneas
são inseguras.

Os dados persistentes ficam em `tesmioloader/build/soviet_mod_loader` (ou no
`state_dir` configurado, sempre relativo ao `baseDir`). Não os inclua no pacote e não recomende apagá-los:
`catalog.ini` mantém IDs e posições estáveis; os índices mantêm sincronização,
confirmação e histórico dos mods.

## Diagnóstico de release

Comece por `tesmioloader.log` e `soviet_mod_loader/report.json`. O log registra
ordem, conflitos, seed e canais de depósitos, fallbacks, componentes
incorporados e hooks filhos. Ao investigar crash, confirme primeiro:

- versão exata do jogo;
- versão/API do TesmioLoader;
- ausência das DLLs standalone e da DLL antiga `000_`;
- conteúdo de `workshop_wip` no intervalo reservado;
- preservação do `catalog.ini` e dos manifestos da partida.

Não confunda os marcos do pipeline. O diagnóstico deve dizer separadamente
`DDS gerado`, `textura carregada`, `resource registrado` e `building validado`.
O manifesto `soviet_mod_loader_deposits.ini` é gravado atomicamente no diretório
físico do mundo sob `media_soviet`; o log inclui caminho e erro Win32 quando a
persistência falha.

Nunca resolva um incidente apagando automaticamente saves ou pastas WIP. O SML
falha fechado nas inconsistências que podem provocar crash e deixa a decisão
destrutiva para o usuário.
