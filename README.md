# AppleTree Pro Gold

Gerenciador de arquivos em árvore para **ProDOS 8**, no estilo dos clássicos gerenciadores
de arquivos do PC dos anos 90, para Apple IIe / IIc / IIgs e Apple II+.

(c) Alexandre Souza - Tabajara Labs - Nov. 2026. Programa gratuito e independente, sem
vínculo com nenhum gerenciador de arquivos comercial.

## O que faz

- Lê o volume inteiro e mostra a **árvore de diretórios**, com uma janela de arquivos e um
  painel de estatísticas (arquivos, bytes, arquivos que casam com o filtro, marcados, espaço livre).
- **Arquivos:** copiar, mover, apagar, renomear, travar/destravar, ver em texto ou hex,
  executar programas SYS/BAS, marcar (tag) e agir sobre todos os marcados.
- **Diretórios:** criar, renomear, apagar, recolher/expandir ramos, "Showall" (todos os arquivos
  do disco numa lista só), filtro com curingas (`*` e `?`), ordenação por nome, tipo, data ou tamanho.
- Imprime a árvore ou a lista de arquivos (impressora no slot 1).
- Tela de entrada com logotipo e **barra de progresso** em porcentagem enquanto o volume é lido.
- Molduras e conectores da árvore em **MouseText** quando a máquina tem (IIe enhanced, IIc, IIgs,
  detectado sozinho); `- | +` nas outras.

## Hardware

| Máquina | Vídeo | Situação |
|---|---|---|
| Apple IIe enhanced (RamWorks) | 80 colunas da placa auxiliar | **testado no hardware real** |
| Apple II+ 64K + Saturn 128K | Videx Videoterm no slot 3 | escrito, ainda não testado |

Limite atual: 164 arquivos e 144 diretórios por volume. O próximo passo é guardar a tabela de
arquivos na memória da RamWorks para chegar a milhares de arquivos (ver `HANDOFF.md`).

## Como usar

Copie `build/ATPG.SYSTEM` (tipo `$FF`, aux `$2000`) para um volume ProDOS, ou use a imagem
`build/appletree.po` (140 KB), e execute `ATPG.SYSTEM`. `?` mostra a ajuda, `Q` sai.

## Como compilar

Precisa do [cc65](https://cc65.github.io) e de Python 3.

```
cl65 -t apple2 -C apple2-system.cfg -Os -Cl -Wl -D,__STACKSIZE__=0x400 -m build/atpg.map -o build/ATPG.SYSTEM src/atpg.c src/plat_a2.c src/a2io.s
python tools/mkpo.py build/appletree.po APPLETREE build/ATPG.SYSTEM:FF:2000
```

(ou `make`). O tamanho da pilha tem de ir para o **linker** (`-Wl -D,...`).

## Estrutura

- `src/atpg.c` — núcleo: interface, árvore, comandos, visualizador
- `src/a2io.s` — drivers de 80 colunas (IIe e Videx), MouseText, teclado, MLI, impressora
- `src/plat_a2.c` — camada ProDOS (cc65); `src/plat_linux.c` — harness de teste no PC
- `tools/atpg_host.py` — carrega e testa o programa num Apple real pela Super Serial Card
  (envia teclas e captura a tela); `tools/mkloader.py` gera o carregador serial sem cc65
- `HANDOFF.md` — estado detalhado, o que foi testado, armadilhas e próximos passos
