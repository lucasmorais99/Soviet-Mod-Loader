# Guia do usuário

O Soviet Mod Loader (SML) carrega mods compatíveis diretamente da Steam
Workshop. Ele reúne em uma única DLL o suporte a construções, recursos,
depósitos e necessidades. Não é necessário instalar esses quatro plugins
separadamente.

## Antes de instalar

Esta versão requer:

- *Workers & Resources: Soviet Republic* v1.1.1.9;
- TesmioLoader b0.3.6;
- Windows.

O jogo deve ser iniciado pelo `tesmiolauncher.exe`, não pelo botão normal da
Steam. Faça backup dos saves importantes antes de alterar um conjunto de mods.

## Instalação

1. Feche o jogo.
2. Abra a pasta `tesmioloader/build/plugins`.
3. Se existirem, apague `000_soviet_mod_loader.dll` e
   `000_soviet_mod_loader.ini`.
4. Remova ou desative DLLs separadas chamadas `buildings.dll`, `resources.dll`,
   `deposits.dll` e `needs.dll`.
5. Copie `soviet_mod_loader.dll` e `soviet_mod_loader.ini` para essa pasta.
6. Abra o `tesmiolauncher.exe`, habilite o SML se a lista de plugins for
   exibida e inicie o jogo.

Não renomeie a DLL para recolocar `000_`. A ordem alfabética deixou de ser um
requisito: o TesmioLoader separa as fases `Init` e `Start`, e o SML controla
internamente seus componentes.

## Uso normal

Inscreva-se em um mod compatível pela Workshop e inicie pelo launcher. O SML:

1. encontra os mods instalados;
2. verifica versões e dependências;
3. define uma ordem estável;
4. mostra conflitos ou incompatibilidades;
5. pede confirmação antes de alterar arquivos;
6. combina o conteúdo e inicia o jogo.

Antes de mostrar a confirmação, o SML verifica se buildings, deposits e needs
apontam para resources existentes e se cada donor de building possui os arquivos
essenciais. Depois da confirmação, ele confere se os quatro componentes realmente
registraram tudo e instalaram os patches necessários. Uma falha crítica encerra
o processo com uma mensagem, em vez de deixar o jogo abrir com ponteiros nulos.

Na primeira execução ou quando a lista, ordem, versão ou estado mudar, uma
janela mostra os mods que serão carregados. Escolha **Carregar mods e iniciar**
para continuar. Recusar, fechar a janela ou pressionar `Esc` encerra o jogo sem
aplicar o novo plano.

A opção `confirmation_mode` em `soviet_mod_loader.ini` aceita:

- `changes` (padrão): pergunta somente quando algo mudou;
- `always`: pergunta em todo início;
- `never`: não mostra janela; útil para execução automatizada ou recuperação
  em ambientes sem interface.

## Estados apresentados

| Estado | Significado |
|---|---|
| active/added | será carregado |
| conflict | será carregado, mas algum conteúdo foi substituído por um mod posterior |
| disabled | o autor ou usuário o desativou no manifesto |
| incompatible | não aceita a API atual do TesmioLoader |
| missing dependency | depende de outro mod ausente ou em versão inadequada |
| error | manifesto ou conteúdo inválido; os demais mods continuam |

Conflitos usam a política “o último vence”. Dependências vêm primeiro; depois
são considerados prioridade, data de adição e ID do mod.

## Aviso sobre `workshop_wip`

Antes da confirmação, o SML compara o catálogo planejado com pastas numéricas
de `media_soviet/workshop_wip` no intervalo `9100000000`–`9199999999`. Uma
pasta inesperada ou com identificação incorreta pode fazer o jogo crashar.

Nesse caso, o SML mostra as pastas problemáticas e impede o início. Feche o
jogo e apague **somente as pastas listadas na mensagem**; depois tente de novo.
O SML nunca as apaga automaticamente. Não remova outras pastas WIP sem saber a
origem delas.

## Atualização

1. Feche o jogo.
2. Apague a DLL e o INI antigos com prefixo `000_`, caso ainda existam.
3. Substitua `soviet_mod_loader.dll` e, se desejado, atualize o INI preservando
   suas escolhas.
4. Não apague a pasta `tesmioloader/build/soviet_mod_loader`.
5. Inicie pelo launcher e confirme o novo conjunto.

A pasta de estado `tesmioloader/build/soviet_mod_loader` contém o catálogo
permanente. Apagá-la pode mudar números internos usados por saves e mods.
Assets gerenciados são colocados no diretório real `tesmioloader/vfs`, nunca
em `tesmioloader/build/vfs`.

## Remoção

Feche o jogo e remova `soviet_mod_loader.dll` e seu INI de
`tesmioloader/build/plugins`. Preserve os saves. Mods que acrescentam recursos,
necessidades, depósitos ou construções podem deixar esses saves dependentes do
mesmo conjunto; volte ao backup se o jogo não conseguir abri-los sem o SML.

## Solução de problemas

**A janela aparece em todo início:** confirme se `confirmation_mode = changes`
e se a Workshop não está atualizando algum item continuamente.

**O jogo fecha ao recusar:** é o comportamento esperado; nenhum novo merge ou
hook é aplicado.

**O jogo não inicia e lista pastas WIP:** apague somente as pastas indicadas e
tente novamente.

**A mensagem cita um resource não registrado:** o mod declarou um building,
deposit ou need que usa um resource ausente. Atualize ou remova o mod apontado;
não tente contornar o bloqueio reduzindo `hook` para `1`.

**A mensagem cita donor, `.nmf` ou `.mtl`:** o building ficou incompleto. O
autor precisa escolher um donor íntegro ou fornecer uma definição compatível.

**O log diz `DDS gerado`, mas o jogo foi bloqueado:** geração, carregamento da
textura, registro do resource e validação do building são etapas diferentes.
O DDS sozinho não torna segura uma produção cujo resource não foi registrado.

**O manifesto de depósitos não é salvo:** confira no log o caminho físico e o
código de erro do Windows. `saved_last`, `campaign1` e `save\<mundo>` são
resolvidos dentro de `media_soviet`; caminhos externos e `..` são recusados.

**A riqueza de um depósito mudou apenas em mundos novos:** isso é esperado.
Autores podem declarar `richness_offset`, mas o SML aplica o balanceamento
somente quando cria um canal novo. Canais já salvos não são regenerados, pois
isso apagaria pintura e depleção existentes.

**Há erro de versão ou prólogo:** o jogo provavelmente foi atualizado. Não
force o carregamento; instale uma versão do SML compatível.

**Mods aparecem como incompatíveis:** confira dependências e se o manifesto do
mod aceita API 4.

**Há crash ou comportamento duplicado:** confirme que não existem
`000_soviet_mod_loader.dll`, uma segunda cópia do SML ou DLLs standalone de
`buildings`, `resources`, `deposits` e `needs`.

Para pedir suporte, envie `tesmioloader.log` e
`tesmioloader/build/soviet_mod_loader/report.json`, além da versão do jogo, do loader
e do SML. Não publique saves pessoais sem verificar seu conteúdo.
