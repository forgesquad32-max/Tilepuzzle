# Tile Puzzle Deluxe (Nintendo 3DS)

Jogo de puzzle deslizante (tudo em inglês dentro do jogo): **45 levels**, **4 modos** e tela de **Settings**.
Feito com devkitARM + libctru + citro2d. Funciona no **Old 3DS**.

## Como gerar o .cia (sem instalar nada no PC)
1. Crie um repositório vazio no GitHub.
2. Envie **todo o conteúdo desta pasta** (incluindo a pasta oculta `.github`) para o repositório (branch `main`).
3. Abra a aba **Actions**. O workflow "Build Tile Puzzle" roda sozinho a cada push (ou clique em *Run workflow*).
4. Quando ficar verde, abra a execução e baixe o artefato **TilePuzzle-3DS** → dentro tem `tilepuzzle.cia` e `tilepuzzle.3dsx`.

## Instalar no Old 3DS (precisa de custom firmware, ex.: Luma3DS)
1. Copie `tilepuzzle.cia` para o cartão SD.
2. Abra o **FBI** → SD → `tilepuzzle.cia` → *Install*.
3. O jogo aparece no menu HOME. (O `.3dsx` serve para abrir pelo Homebrew Launcher, em `/3ds/`.)

## Modos
- **Classic**: 45 levels (3x3, 4x4, 5x5), até 3 estrelas por level, levels vão liberando.
- **Time Attack**: 2:00 no relógio, cada tabuleiro resolvido dá +20s.
- **Move Limit**: resolva dentro do limite de movimentos e monte uma sequência (streak).
- **Zen**: sem pressão.

## Settings
Theme (5 cores), Animations, Timer, D-Pad (move a peça ou o espaço vazio) e Reset Progress.
O progresso é salvo em `sdmc:/3ds/tilepuzzle/save.dat`.

## Mexer no jogo
- Levels / dificuldade: `source/game.c` (função `level_info`).
- Textos, cores, modos e telas: `source/main.c`.
- Nome do jogo/autor no HOME: `Makefile` (APP_TITLE, APP_AUTHOR).
- `make test` roda os testes da lógica no PC.
