# DISCLAIMER: Artificial Intelligence (AI) has been used throughout developing this project. It has enabled me, a long-time fan of modding games with an average technical expertise, to share my passion with many others around the world.

# Soviet Mod Loader

O Soviet Mod Loader (SML) 0.5.2 é um plugin único para o TesmioLoader que descobre,
ordena e combina mods diretamente da Steam Workshop. A DLL incorpora as
funcionalidades de `buildings`, `resources`, `deposits` e `needs`; portanto, o
usuário não precisa instalar esses plugins separadamente.

Esta versão acompanha o TesmioLoader b0.3.6, usa a API 4 e é destinada ao
*Workers & Resources: Soviet Republic* v1.1.1.9. O TesmioLoader distribuído
fornece a infraestrutura de carregamento; o SML fornece, em uma única DLL, o
fluxo de conteúdo adotado por este projeto.

## Escolha seu guia

- [Guia do mantenedor](docs/GUIA_MANTENEDOR.md): sincronização com o upstream,
  arquitetura, build, testes e publicação.
- [Guia do usuário](docs/GUIA_USUARIO.md): instalação, atualização, confirmação
  de mods e solução de problemas.
- [Guia para desenvolvedores de mods](docs/GUIA_MODDERS.md): manifesto,
  fragmentos INI, assets, dependências e hooks nativos.

A referência interna mais detalhada está em
[Arquitetura](docs/ARCHITECTURE.md). Um mod mínimo funcional está em
[`template/simple-mod`](template/simple-mod).

## Instalação rápida

1. Feche o jogo e apague versões antigas chamadas
   `000_soviet_mod_loader.dll` e `000_soviet_mod_loader.ini`.
2. Copie `soviet_mod_loader.dll` e `soviet_mod_loader.ini` para
   `tesmioloader/build/plugins`.
3. Não instale DLLs separadas de `buildings`, `resources`, `deposits` ou
   `needs` junto com o SML.
4. Inicie pelo `tesmiolauncher.exe`.

O SML não depende mais dos INIs que acompanhavam os plugins standalone. Quando
há conteúdo, ele impõe internamente o modo funcional mínimo de `resources`,
`deposits`, `needs` e `buildings`. Antes de iniciar, também verifica referências
entre esses quatro catálogos e interrompe a carga se um resource, donor ou asset
essencial estiver ausente.

O prefixo `000_` não é mais utilizado. Nunca mantenha as duas DLLs na mesma
instalação: ambas seriam descobertas e poderiam instalar lógica duplicada.

## Fontes upstream

O contrato de integração vem do
[TesmioLoader](https://github.com/MaxLegend/TesmioLoader), em especial da
[API pública](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h),
da [documentação de plugins](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/09-plugins.md)
e do [changelog](https://github.com/MaxLegend/TesmioLoader/blob/master/changelog.md).
As adaptações e diferenças intencionais estão documentadas no guia do
mantenedor e no `NOTICE.md`.
