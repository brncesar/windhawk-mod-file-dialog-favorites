# FileWindowFavoriteLocations — plano de implementação

## Objetivo

Criar um mod do Windhawk compatível com Windows 11 que adicione um botão de pasta à direita da barra de título das janelas de abrir e salvar arquivos. Clicar no botão abre um menu com locais favoritos definidos pelo usuário; escolher um local navega até ele na própria janela, sem abrir ou salvar automaticamente e preservando o nome do arquivo e o filtro selecionado.

Este documento é o ponto de partida para continuar o trabalho no VSCode, na pasta `C:\Repositories\FileWindowFavoriteLocations`. O projeto ainda não foi implementado. O repositório público foi criado em 2026-10-11 na conta pessoal `brncesar`: <https://github.com/brncesar/windhawk-mod-file-dialog-favorites> (a pasta local mantém o nome `FileWindowFavoriteLocations`).

## Decisões registradas (2026-10-11)

Resultado da sessão de definição antes da implementação. O vocabulário está em [CONTEXT.md](CONTEXT.md); decisões difíceis de reverter ficam em [docs/adr/](docs/adr/).

### Produto

- O Favorites Button aparece nas três variantes do File Dialog moderno: Open Dialog, Save Dialog e Folder Picker. O Legacy File Dialog fica para o adaptador da etapa 5.
- Posição: imediatamente à esquerda do botão padrão mais à esquerda da barra de título, com tamanho e hover iguais aos botões padrão. Ícone: glifo de pasta da Segoe Fluent Icons, monocromático, acompanhando claro/escuro.
- Estado: esmaecido quando o diálogo está inativo; desativado enquanto o diálogo estiver bloqueado por uma janela filha (ex.: confirmação de substituição).
- Favorite = nome de exibição + caminho de pasta (local ou UNC), aceitando variáveis de ambiente. Locais do shell que não são caminhos ficam para depois.
- Favorites Menu: favoritos na ordem das configurações, com ícone de pasta. Nome vazio exibe a última pasta do caminho. Lista vazia exibe um item desativado indicando as configurações do mod no Windhawk. Separadores e submenus ficam para a etapa 6.
- Destino indisponível: nada é verificado ao abrir o menu; se a navegação falhar, o diálogo permanece onde está e um aviso não bloqueante é exibido.
- Primeira versão utilizável: favoritos editados apenas nas configurações do Windhawk. “Adicionar pasta atual” fica para um marco posterior, com desenho próprio de armazenamento.
- Teclado: apenas mouse na prova de conceito. Na v1, atalho configurável (padrão proposto Ctrl+Shift+F, sujeito a verificação de conflito); setas navegam o menu e Esc fecha.

### Técnica

- Identificador do mod: `file-dialog-favorites`; nome: “File Dialog Favorites”.
- Licença MIT, sem código do FileBox eXtender (GPLv3 ou posterior, v2.01.00). Ver [ADR 0001](docs/adr/0001-mit-license-no-fbx-code.md).
- Detecção: interceptar `IFileDialog::Show` no objeto real do diálogo (técnica do mod quick-explorer-switcher), cobrindo chamadas COM e APIs antigas redirecionadas.
- Navegação: `IFileDialog::SetFolder` com o diálogo aberto, guardando e restaurando o Typed File Name e o File Type Filter.
- Botão: começar pela janela auxiliar pertencente ao diálogo, posicionada por `DWMWA_CAPTION_BUTTON_BOUNDS`. Desenho na área não cliente só será prototipado se a janela auxiliar falhar nos critérios da etapa 3, com a causa documentada.
- Processos: lista explícita na prova de conceito; depois `*` com as exclusões críticas do Windhawk. Exclusões por aplicativo usam as listas de inclusão/exclusão próprias do Windhawk.
- Fluxo de desenvolvimento: o arquivo do mod no repositório é a fonte da verdade; verificação de sintaxe com o clang do Windhawk e as mesmas opções do editor; instalação pelo editor do Windhawk.
- Programa de teste próprio compilado com o clang do Windhawk, em x86 e x64.
- Matriz de validação inicial: Bloco de Notas, Paint, Edge/Chrome, um app do Office e o programa de teste.
- Idioma: README, código, CONTEXT.md e ADRs em inglês; este plano permanece em português.

### Ambiente de teste disponível

Windows 11 26H2 build 26300.9550 x64, Windhawk 1.7.3.

## 1. Criar o repositório público no GitHub

- Confirmar a identidade autenticada no GitHub e verificar que corresponde à conta pessoal do usuário. Não escolher uma organização nem deduzir o usuário pelo nome da pasta.
- Verificar se já existe um repositório com esse nome na conta pessoal. Se existir, verificar sua finalidade antes de reutilizá-lo; não sobrescrever conteúdo.
- Verificar o conteúdo local e as instruções aplicáveis à pasta antes de inicializar o Git.
- Criar o repositório público `windhawk-mod-file-dialog-favorites` na conta pessoal e conectá-lo à pasta local.
- Usar uma descrição como: “Favorite folder menu in Windows Open/Save dialogs, implemented as a Windhawk mod for Windows 11.”
- Registrar neste documento o proprietário e a URL confirmados. Publicar inicialmente o plano, um README explicando o estado experimental e um `.gitignore` apropriado. Não publicar arquivos pessoais, caminhos de favoritos reais ou credenciais.
- Se autenticação, acesso de rede ou criação remota estiverem indisponíveis, pedir apenas o acesso necessário. Não declarar o repositório criado antes de confirmar sua existência e visibilidade pública.

**Entrega:** repositório público confirmado, remoto local configurado e primeiro commit publicado. A criação será executada na continuação da tarefa; este plano não a executa.

## 2. Levantar comportamento e estudar as referências

- Baixar os fontes do FileBox eXtender pela página oficial, verificar o conteúdo do arquivo e ler a licença incluída. O site informa GNU GPL e uma solução C++ para Visual Studio 2008; a versão exata e os termos presentes no pacote ainda precisam ser conferidos.
- Documentar os mecanismos usados para identificar janelas, adicionar botões e navegar para outra pasta. Registrar quais dependem de APIs antigas ou detalhes internos do Windows.
- Estudar o mod Better Dialogs como referência para interceptação de janelas de arquivos. Ele demonstra modificações desses diálogos, mas não prova a implementação do botão desejado na barra de título.
- Escolher e documentar a estratégia de licenciamento antes de incorporar código de terceiros. Código público não significa código sem condições de reutilização. Não atribuir uma licença incompatível ao código reaproveitado.
- Registrar a versão do Windhawk, as versões/builds do Windows 11 e as arquiteturas efetivamente disponíveis para teste.

**Entrega:** nota técnica curta com abordagem, origem de qualquer código reaproveitado e licença escolhida.

## 3. Prova de conceito: botão e navegação

Este é o marco que decide a viabilidade da experiência original. Não investir primeiro em um editor completo de favoritos.

- Implementar o mod em C++ e começar por aplicativos de teste explicitamente selecionados.
- Capturar o ciclo de vida das janelas modernas de abrir/salvar e associar a janela à interface de diálogo correta. Não identificar um diálogo apenas pelo título traduzido ou pela classe genérica `#32770`.
- Avaliar interceptação da criação/exibição dos objetos de diálogo e utilização de `IFileDialog`, mantendo chamadas na thread apropriada e referências COM com vida útil controlada.
- Implementar um botão com ícone de pasta na barra de título, antes do grupo de botões padrão existente. Não presumir que toda janela de arquivo tem minimizar/maximizar.
- Comparar desenho na área não cliente e uma janela auxiliar pertencente ao diálogo. Escolher por comportamento comprovado, incluindo recorte, foco, posição e descarregamento. Um botão dentro do conteúdo do diálogo pode servir para depuração, mas não satisfaz o requisito visual final.
- Exibir um menu com três pastas de teste e navegar pela interface apropriada, avaliando `IFileDialog::SetFolder` nos diálogos modernos.
- Preservar o texto do nome do arquivo, extensão e filtro. Não substituir o diálogo inteiro nem alterar o comportamento de confirmação do aplicativo.
- Verificar que o botão acompanha mover, redimensionar, ocultar, reexibir e fechar a janela, sem cobrir controles padrão ou bloquear arrastar a janela.

**Aceite:** em janelas de abrir e salvar do Windows 11, o botão abre o menu e troca a pasta sem confirmar a operação, perder o nome digitado, alterar o filtro ou produzir botões duplicados. Registrar capturas e aplicativos/builds testados.

**Decisão:** se o posicionamento exato falhar, documentar a causa e os protótipos antes de propor uma mudança de experiência ao usuário. Não trocar silenciosamente o requisito por um botão em outro lugar.

## 4. Primeira versão utilizável: favoritos

- Adicionar cadastro, edição de nome e caminho, ordenação e remoção de favoritos. Começar pelas configurações do Windhawk se elas atenderem à usabilidade necessária.
- Manter uma lista comum entre aplicativos. Avaliar configurações do Windhawk para dados estáticos e armazenamento compartilhado para alterações pelo menu; não presumir que um valor gravado em um processo atualiza instantaneamente os outros.
- Oferecer “Adicionar pasta atual”, com detecção de duplicatas, e atualização da lista nos processos já abertos.
- Se houver arquivo compartilhado, usar escrita atômica, controle de concorrência e versão do formato. Evitar que gravações simultâneas percam favoritos.
- Aceitar caminhos Unicode, pastas locais e locais de rede. Tratar pastas removidas e compartilhamentos indisponíveis sem bloquear a interface.
- Permitir abrir o menu pelo teclado, navegar pelos itens e cancelar com Esc. Oferecer identificação acessível do botão.
- Evitar recursão quando a própria configuração abrir uma janela de seleção de pasta.

**Aceite:** favoritos persistem após reiniciar os aplicativos e o Windhawk; mudanças aparecem nos aplicativos atendidos; destinos inválidos não fecham nem travam a janela.

## 5. Compatibilidade, estabilidade e testes

- Adicionar um adaptador separado para os diálogos antigos baseados em `GetOpenFileName`/`GetSaveFileName`, preservando callbacks, controles personalizados e retorno da operação.
- Manter os diálogos modernos e antigos com suas respectivas implementações; não modernizar automaticamente os antigos como efeito colateral.
- Testar aplicações de 32 e 64 bits em Windows 11 x64. Avaliar ARM64 separadamente, incluindo processos emulados; só declarar suporte após validação.
- Testar claro/escuro, alto contraste, escalas 100%, 125%, 150% e 200%, troca de monitor e navegação por teclado.
- Usar programas de teste próprios para cobrir APIs e selecionar aplicativos reais disponíveis, como editor de texto, navegador e suíte de escritório. Registrar versões e tipo de diálogo observado.
- Verificar conflitos com outros mods que alterem as mesmas janelas, inclusive Better Dialogs. Não presumir coexistência.
- Testar abrir/cancelar repetidamente, vários diálogos em processos distintos, alteração de favoritos durante uso e desativação do mod com janela aberta.
- Remover subclasses, botões, eventos e referências antes do descarregamento. Nenhum callback pode continuar apontando para código descarregado.
- Evitar consultas lentas à rede na thread da interface. Não salvar nomes de arquivos ou histórico pessoal em logs por padrão.
- Expandir os aplicativos atendidos gradualmente. Respeitar exclusões do Windhawk, processos protegidos e aplicativos incompatíveis. Não contornar bloqueios de segurança.

**Aceite:** matriz de compatibilidade documentada, testes automatizados para persistência/concorrência e regras relevantes, e testes visuais/manuais para integração. Nenhuma falha conhecida que comprometa abrir/salvar nos aplicativos declarados compatíveis.

## 6. Recursos adicionais do FileBox eXtender

A primeira versão deve concluir a experiência de favoritos descrita pelo usuário. Para equivalência mais ampla com o programa original, desenvolver depois:

- Submenus, separadores e atalhos de teclado.
- Histórico de pastas, com limite, limpeza e opção de desativação.
- Importação/exportação dos favoritos; avaliar importação do formato original após estudá-lo.
- Botões em janelas do Explorer, com adaptador próprio para navegação.
- Fixação de janelas no topo e recolhimento à barra de título.
- Ajustes de tamanho e apresentação dos diálogos e exceções por aplicativo.

**Entrega:** comparar o inventário de recursos originais com os implementados, indicando suporte, pendências e diferenças. Não anunciar equivalência completa apenas por concluir favoritos.

## 7. Distribuir e manter

- Entregar código do mod, licença, instruções de instalação/desativação, capturas e limitações conhecidas.
- Validar instalação local pelo editor do Windhawk antes de preparar submissão ao catálogo oficial. Publicação no catálogo depende do processo de revisão e não deve ser prometida.
- Criar versões no repositório público com descrição objetiva dos testes realizados.
- Documentar como reportar um problema informando build do Windows, Windhawk, aplicativo, tipo de diálogo e passos para reproduzir.
- Manter um procedimento de verificação após atualizações do Windows, sobretudo para o posicionamento na barra de título e qualquer detalhe interno utilizado.

## Arquitetura proposta

- **Integração Windhawk:** carregamento nos aplicativos selecionados, interceptações e ciclo de vida.
- **Adaptadores de diálogo:** moderno e antigo; Explorer em etapa posterior.
- **Botão e menu:** apresentação, posicionamento, mouse, teclado e acessibilidade.
- **Favoritos:** validação, ordenação, persistência e sincronização entre processos.

Começar com um mod autocontido, respeitando o formato de distribuição do Windhawk. Separar responsabilidades no código sem exigir inicialmente um aplicativo auxiliar. Se um editor mais completo justificar um componente externo, documentar a necessidade e o protocolo de comunicação antes de adicioná-lo.

Um aplicativo independente continua sendo opção futura, mas precisará de seu próprio mecanismo de integração, instalação e suporte por arquitetura. A prova de conceito deve esclarecer a necessidade antes dessa decisão.

## Limites iniciais

- Suporte inicial a diálogos padrão do Windows em aplicativos validados.
- Diálogos próprios, seletores hospedados em outros processos e aplicativos protegidos exigem avaliação específica.
- Compatibilidade ampla e posicionamento fiel são objetivos a demonstrar, não garantias já comprovadas.
- O código original ainda não foi inspecionado; as fontes abaixo fundamentam a proposta técnica.

## Referências

- FileBox eXtender: <https://www.hyperionics.com/files/>
- Fontes e binários: <https://www.hyperionics.com/files/downloads.asp>
- Criação de mods: <https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod>
- Processos e exclusões: <https://github.com/ramensoftware/windhawk/wiki/Injection-targets-and-critical-system-processes>
- Better Dialogs: <https://github.com/ramensoftware/windhawk-mods/blob/main/mods/better-dialogs.wh.cpp>
- IFileDialog: <https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifiledialog>
- IFileDialogCustomize: <https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifiledialogcustomize>

## Instrução para retomar no VSCode

“Leia PLAN.md e as instruções aplicáveis a esta pasta. Execute o passo 1: confirme minha conta pessoal autenticada, crie o repositório público windhawk-mod-file-dialog-favorites e publique os arquivos iniciais. Depois estude os fontes e avance para a prova de conceito do mod Windhawk. Preserve o requisito de botão de pasta na barra de título e registre os resultados de compatibilidade. Não declare funcionalidades prontas sem testá-las.”
