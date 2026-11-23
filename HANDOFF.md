# AppleTree Pro Gold - handoff para a sessao local (Windows, COM9)

Projeto: gerenciador de arquivos estilo arvore para ProDOS 8, em C (cc65) + asm 6502.
Alvos: Apple IIe enhanced (RamWorks >= 256K) e Apple II+ 64K + Saturn 128K com
placa Videx de 80 colunas no slot 3.

## Estrutura
- src/atpg.c       nucleo (UI, arvore, comandos, visualizador)
- src/plat.h       interface de plataforma
- src/plat_a2.c    ProDOS (cc65 dirent/open + chamadas MLI)
- src/a2io.s       drivers de 80 col (IIe aux / Videx), teclado, MLI, impressora,
                   buffers ProDOS fixos em $1800/$1C00, modo de teste serial
- src/plat_linux.c harness de teste no Linux (tela falsa + teclas por script)
- tools/loader.s   carregador serial de 194 bytes em $0300 (SSC slot 1)
- tools/atpg_host.py  script do PC (pyserial): boot/upload/run/keys/snap
- tools/mkpo.py    gera imagem .po ProDOS de 140K
- build/ATPG.SYSTEM, build/appletree.po, build/loader.bin  (ja compilados)

## Mapa de memoria (Apple)
$0800-$13FF arvore de diretorios (144 x 21 bytes) | $1400 order | $1490 vlist |
$1600 lista de volumes | $1700 checkpoints do visualizador | $1800-$1FFF 2 buffers ProDOS |
$2000-$BEFF programa (cc65 apple2-system.cfg, pilha C $400) | LC $D400-$DFFF visualizador.
ATENCAO heap: o opendir do cc65 faz malloc de ~590 bytes, no heap entre o fim do BSS e a pilha. O tamanho
da pilha tem de ir para o LINKER (`-Wl -D,__STACKSIZE__=0x400`); `-D __STACKSIZE__` sozinho vai so para o
compilador e a pilha fica com 2 KB -> heap pequeno -> volumes aparecem vazios (era o "I/O error" de 2026-11-22).
Com $400 sobram ~1,4 KB de heap. Se o BSS crescer, conferir no build/atpg.map.
Limite atual: 164 arquivos / 144 diretorios (tabela de arquivos na RAM principal).

## Teste no hardware (SSC slot 1, 19200, COM9) -- funcionou em 2026-11-22
1. No IIe: bootar ProDOS + BASIC.SYSTEM, digitar `IN#1` e `PR#1` (a tela fica sem cursor: normal).
   (So um programa pode abrir a COM9: feche a ponte do CFFA antes.)
2. `python tools/atpg_host.py COM9 all build/ATPG.SYSTEM "{DOWN}{RET}"`
   - digita o loader no Monitor ($0300-$03C3) e confere; carrega o programa em blocos de 256 com ACK $06
     (um '.' por bloco na ultima linha do Apple); confere `S=xxxx`; manda `J` e captura a tela.
   - depois: `python tools/atpg_host.py COM9 keys "..."` e `... snap`
   - na captura, letras minusculas / '#' = video inverso
   - teclas: {UP} {DOWN} {LEFT} {RIGHT} {RET} {ESC} {^X} {@C}=Open-Apple+C {SNAP} {WAIT}
3. Licoes (sem controle de fluxo na SSC): o PC manda com 2 stop bits e so apos o ACK de cada bloco;
   o Apple manda ASCII com bit 7 ligado; nunca usar COUT depois de carregar sobre $9A00+ (mata o BASIC.SYSTEM,
   dono do gancho PR#1); no pyserial/Windows, mudar o timeout logo apos write() pode perder o byte.
   O loader e gerado por `python tools/mkloader.py` (montador 6502 minimo embutido, sem cc65).
4. Alternativa sem serial: copiar build/ATPG.SYSTEM (tipo $FF, aux $2000) para o CFFA, ou usar build/appletree.po.

Observado no IIe real: tela e teclado IIe (80STORE/PAGE2, ALTCHARSET, inverso) OK, arvore/estatisticas OK,
modo de teste serial OK. A tela aparece deslocada ~2 colunas para a esquerda: e o CPLD modificado da RamWorks
do usuario, nao o programa.

## Testado no IIe enhanced em 2026-11-22 (pela serial)
Arvore, ajuda, Avail, Log (com barra de progresso), janela de arquivos normal/expandida, ^F, Tag/Untag,
Showall, Filespec, ^S, View texto/hex, collapse/expand; no /RAM: Makedir, Copy, Attributes, Rename (arquivo
e diretorio), Move, Delete (arquivo e diretorio); Print (sai pela SSC do slot 1); Execute (BASIC.SYSTEM).
iobuf_alloc/iobuf_free e a troca LC/ROM funcionam (opens e Execute OK).

## Tela de entrada e malloc
- splash(): moldura normal, APPLE/TREE em letras 3x5 (cada pixel = 3 celulas de '_' inverso: cortes
  horizontais), "P R O  G O L D", (c) Alexandre Souza - Tabajara Labs - Nov. 2026, versao; barra de progresso
  na linha 22 e "FREE - Gratuito - ..." na 23. Estatisticas aparecem ao fim do log; ~1,5 s ou tecla.
- malloc/free proprios em plat_a2.c devolvem o iobuf (o unico usuario e o opendir do cc65, so durante o log):
  o heap nao importa mais. Se outro codigo passar a usar malloc, rever isso.
- Visualizador abre binarios em hex (texto so para TXT $04 e SRC $B0).

## Pontos ainda NAO verificados em hardware
- driver Videx (bancos $C0B0+4n, RAM $CC00-$CDFF, bit 7 = inverso) - escrito de memoria
- Execute de programa BAS; comandos em arquivos marcados (^A/OA+tecla). (Quit testado: volta ao BASIC.)
- Modo de teste: teclas pela serial se perdem se chegarem com o AT ocupado (a SSC guarda 1 byte);
  o atpg_host espera 0,6 s entre teclas. No Git Bash use MSYS_NO_PATHCONV=1 ao mandar caminhos "/VOL/...".

## Proximo passo planejado
Mover a tabela de arquivos para memoria de expansao para suportar milhares de arquivos:
- IIe: bancos 1..n da RamWorks via $C073 (banco 0 tem /RAM e a pagina de texto aux).
  Leitura aux exige RAMRD com o codigo rodando no LC (segmento "LC") ou pagina 0/1;
  escrita pode usar RAMWRT de qualquer lugar. SEI durante o acesso.
- II+: perguntar o slot da Saturn 128K na inicializacao; bancos 1..7 em $D000-$FFFF
  (selecao $C084-$C087 / $C08C-$C08F + 16*slot); rotinas de acesso na RAM principal,
  restaurar banco 0 e `$C080` (LC banco 2 para o runtime cc65) depois; SEI.
- arquivos agrupados por diretorio (first/count/cap por no) + cache de registros
  em RAM principal; estatisticas incrementais.

### Plano para a proxima sessao (combinado com o usuario: depois de 27/11/2026)
Escopo 1: so IIe + RamWorks (o II+/Saturn nao da para testar agora). Trabalhar em etapas que deixam o AT
funcionando ao fim de cada uma, porque a cota de uso do usuario e curta.
1. Rotinas em a2io.s (asm, SEI/CLI em volta):
   - `xm_init()`: detecta quantos bancos a RamWorks tem (escreve/le assinatura em cada banco via $C073,
     com cuidado para nao destruir o banco 0 = /RAM + texto aux) -> numero de bancos utilizaveis.
   - `xm_get(u16 idx, struct file *dst)` / `xm_put(u16 idx, const struct file *src)`: registro de 30 bytes.
     Copiar via buffer em pagina 0/1 ou codigo no segmento LC, porque ligar RAMRD troca toda a leitura
     de $0200-$BFFF para aux (o codigo em RAM principal some). RAMWRT basta para escrever.
   - Teste isolado pela serial antes de mexer no atpg.c (ex.: tecla de debug que grava/le 1000 registros).
2. atpg.c: trocar `files[i].campo` por acesso via cache (ex.: `struct file *F(u16 i)` com 1-2 registros
   em cache, marcados sujos e gravados de volta). Pontos de uso: grep `files[`. MAXFILES passa a vir de
   xm_init (64 KB por banco / 30 bytes ~ 2180 arquivos por banco). Sem RamWorks: manter a tabela atual
   na RAM principal (164).
3. vlist (u16 por arquivo visivel, hoje em $1490, 184 entradas) e order tambem limitam: levar vlist para
   um banco aux ou recalcular sob demanda.
4. Testar no IIe com um volume grande (/TOTAL.REPLAY ou /SAN32 do CFFA) pela serial: log, showall,
   sort, tag, copy.
Pendencias menores: tela de entrada fica ~1,5 s (usuario nao conseguiu ver o logotipo 5x5): esperar tecla
ou ~4 s; testar Execute de .BAS e comandos em arquivos marcados.

Uso de cota: esta conversa (2026-11-22) passou de 450 mil tokens de contexto; comecar a proxima numa
sessao NOVA a partir deste HANDOFF sai bem mais barato.

## Compilar
cc65 snapshot "V2.19 - Git eebf54a" instalado em C:/Users/Alexandre/cc65 (bin/cl65). Nesse snapshot a data/hora
do dirent ficam em d_mtime.date / d_mtime.time (plat_a2.c ja ajustado). No Git Bash:
`PATH=/c/Users/Alexandre/cc65/bin:$PATH cl65 -t apple2 -C apple2-system.cfg -Os -Cl -D __STACKSIZE__=0x300 -m build/atpg.map -o build/ATPG.SYSTEM src/atpg.c src/plat_a2.c src/a2io.s`
e `python tools/mkpo.py build/appletree.po APPLETREE build/ATPG.SYSTEM:FF:2000`.
Harness no Windows (mingw): gcc com um -include que defina `mkdir(a,b)` como `_mkdir(a)`; `ATPG_MT=1` simula MouseText.

## MouseText (2026-11-22, verificado no IIe enhanced)
scr_init le $FBC0 com a ROM ligada ($E0 = IIe enhanced/IIgs, $00 = IIc) e liga scr_mtext. As molduras usam os
glifos G_* de plat.h: topo '_', base $4C, lado esquerdo $5A, lado direito/divisoria $5F, divisorias $53, cantos em
branco (MouseText nao tem cantos nem tes). Sem MouseText (IIe antigo, Videx) saem - | +.
Conectores da arvore com MouseText (escolha do usuario): vertical $5A, galho = $5A + $53, ultimo galho = $54 + $53.
O $54 tem a vertical na borda ESQUERDA e o traco EMBAIXO, por isso o `└` nao emenda perfeito; a variante toda
em $54/$5F com traco '_' foi testada no IIe e o usuario achou feia.
