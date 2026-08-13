# Guia para desenvolvedores de mods

O SML lê o mod diretamente da pasta da Steam Workshop. O contrato é um
manifesto `soviet.mod.ini`, fragmentos de conteúdo opcionais, assets com os
caminhos finais do VFS e hooks nativos opcionais.

Comece copiando [`template/simple-mod`](../template/simple-mod). O template é
um exemplo mínimo e pode ser reduzido aos domínios usados pelo seu mod.

## Estrutura recomendada

```text
meu-mod/
├── soviet.mod.ini
├── tesmio/
│   ├── buildings.ini
│   ├── resources.ini
│   ├── deposits.ini
│   └── needs.ini
├── assets/
│   └── media_soviet/...
└── hooks/
    └── meu_hook.dll
```

O caminho dentro de `assets` deve ser exatamente o caminho desejado dentro de
`tesmioloader/vfs`. Não inclua arquivos gerados, catálogos internos ou cópias
dos INIs finais do loader.

## Manifesto

Exemplo:

```ini
[mod]
id = org.exemplo.industria.simples
name = Indústria Simples
version = 1.0.0
enabled = 1
priority = 0
tesmio_api_min = 3
tesmio_api_max = 4

[dependencies]
org.exemplo.biblioteca = >=1.2.0

[content]
resources = tesmio\resources.ini
deposits = tesmio\deposits.ini
needs = tesmio\needs.ini
buildings = tesmio\buildings.ini
assets = assets

[hooks]
dll = hooks\meu_hook.dll
```

Regras:

- `id`: identificador global e permanente, preferencialmente DNS reverso; não
  o altere depois de publicar.
- `version`: versão semântica do mod.
- `enabled`: `0` desativa o item inteiro.
- `priority`: desempate explícito; valores maiores são aplicados mais tarde.
- `tesmio_api_min/max`: intervalo da API do host aceito pelo mod.
- dependências: uma entrada `id = restrição` por mod necessário.
- `dll`: pode se repetir para mais de um hook.

A ordem final coloca dependências antes dos dependentes e depois considera
prioridade, data de adição e ID. Quando dois mods definem o mesmo conteúdo, o
posterior vence somente naquela entrada; o restante do mod perdedor continua.

## Catálogo interno: números que o mod não declara

Para evitar colisões entre autores, o SML mantém identificadores numéricos
estáveis no `catalog.ini`. Portanto:

- não declare `id` em uma seção de `buildings.ini`;
- não declare `type`, `map` ou `component` em `deposits.ini`;
- não prefixe recursos ou necessidades com slots numéricos explícitos;
- use nomes de seção estáveis e únicos dentro do seu mod.

O SML atribui IDs de construções, tipos de depósitos e posições de listas de
forma append-only. Nos depósitos, o resultado consolidado usa `map = auto`; o
componente incorporado escolhe mapa e canal. Cada depósito ainda não
inicializado recebe distribuição própria por partida, e o manifesto do mundo
preserva os canais existentes.

Cada seção pode ajustar somente a geração de canais novos:

```ini
[simple_ore]
token = $TYPE_MINE_SIMPLE
radius = ore
icon = simple_parts
richness_offset = 0.03
```

`richness_offset` aceita valores de `-0.25` a `+0.25`. O valor é somado ao
ruído antes do limiar: números positivos tornam as jazidas maiores e mais
ricas; negativos as tornam menores e mais fracas. O padrão `0.00` reproduz a
geração anterior. Valores inválidos bloqueiam a carga antes da confirmação.
Alterar a chave não regenera canais já registrados no manifesto do mundo, para
preservar pintura, depleção e dados do save.

Não dependa do número que apareceu em uma instalação específica. A identidade
portável é `id do mod + nome da seção`.

## Fragmentos de conteúdo

Copie a sintaxe dos quatro arquivos do template. Inclua apenas seções do seu
mod; configurações globais são protegidas, salvo quando o manifesto de conteúdo
permite explicitamente `allow_settings = 1`.

Para `resources` e `needs`, conflitos são resolvidos por seção/chave. Para
`buildings` e `deposits`, a seção nomeada é atômica, preservando chaves
repetíveis como `line`.

O SML valida estaticamente referências usadas por `$PRODUCTION`,
`$CONSUMPTION`, `$CONSUMPTION_PER_SECOND`, storages, clones, ícones de deposits
e needs. Use sempre o nome textual estável do resource. Uma referência ausente
é fatal para o conjunto inteiro antes de qualquer INI ou asset ser aplicado.

Um building também precisa de um donor vanilla íntegro. O SML exige pelo menos
`media_soviet/buildings_types/<donor>.ini`,
`media_soviet/buildings/<donor>.nmf` e
`media_soviet/buildings/<donor>.mtl`. A geração `INCOMPLETE` é tratada como
erro fatal; teste o donor na mesma versão do jogo suportada pelo SML.

Use tokens Tesmio em ASCII e salve INIs como UTF-8 sem BOM. Nomes exibidos ao
usuário podem usar Unicode quando o formato de origem aceitar.

## Assets

Coloque texturas e demais arquivos abaixo de `assets/media_soviet/...`. O SML
copia apenas bytes alterados para o VFS real. Um asset removido do mod só é
apagado do VFS se ainda for idêntico ao último arquivo escrito pelo SML, para
não destruir uma edição local.

Evite que dois mods publiquem o mesmo caminho. O conflito será determinístico,
mas o resultado dependerá da ordem resolvida e será informado ao usuário.

## Hooks nativos

Um hook é um plugin TesmioLoader comum listado em `[hooks]`. Compile-o para
Windows x64 com runtime estático e os cabeçalhos da API suportada. Exporte:

```cpp
extern "C" __declspec(dllexport) uint32_t TsmPluginApiVersion();
extern "C" __declspec(dllexport) int TsmPluginInit(
    const TsmHost* host, TsmPluginInfo* info);
extern "C" __declspec(dllexport) int TsmPluginStart(); // opcional
```

Use `TsmPluginInit` para validar o host, ler configuração e publicar serviços.
Consuma serviços de outros plugins e instale hooks dependentes somente em
`TsmPluginStart`. Verifique `host->structSize` antes de ler campos acrescentados
pela API 4, como `vfsRoot`. Um erro deve retornar código não zero e desativar
apenas o hook, sem encerrar o jogo.

Confira a [documentação upstream de plugins](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/09-plugins.md)
e a [API pública](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h).
Hooks que modificam código do jogo precisam validar versão e bytes do prólogo;
nunca aplique endereços de uma versão em outra.

## Teste antes de publicar

1. Trabalhe em uma cópia sob `media_soviet/workshop_wip` e mantenha fora do
   intervalo interno reservado `9100000000`–`9199999999`.
2. Use um `id` definitivo desde o primeiro teste.
3. Inicie pelo `tesmiolauncher.exe` e confira a ordem na confirmação.
4. Verifique `tesmioloader.log` e `soviet_mod_loader/report.json`.
5. Teste jogo novo, save/reload e remoção/reinstalação do mod.
6. Teste dependência ausente e versões incompatíveis.
7. Teste junto aos mods que podem declarar conteúdo semelhante.
8. Publique somente os arquivos de origem do mod; não publique o catálogo nem
   os DDS intermediários/persistidos do seu ambiente.
9. No log, confirme separadamente: resource registrado, deposit aceito, textura
   carregada e building completo. Uma linha de DDS gerado não substitui as
   outras três verificações.

Adicionar ou remover recursos, necessidades, construções e depósitos pode
alterar dados serializados. Documente a compatibilidade de saves e recomende
backup aos usuários.
