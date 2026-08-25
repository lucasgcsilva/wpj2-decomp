/* Legendas PT-BR aplicadas ao recurso textual completo de Wonder Project J2.
 * A camada so existe quando WPJ2_LEGENDAS aponta para o TSV, ou vale 1. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "legendas.h"

#define RDRAM_LIMIT          0x00400000u /* ponteiros de texto normais do jogo */
#define RDRAM_DUMP_BYTES     0x00800000u /* mapeamento completo do host */
#define SCRATCH_SLOT_BYTES   1024u
#define MAX_ENTRIES          8192u
#define MAX_TEXT             511u
#define HASH_BUCKETS         8191u

typedef struct {
    char* source;
    char* translated;
    uint16_t source_len;
    int next;
} subtitle_entry_t;

static subtitle_entry_t g_entries[MAX_ENTRIES];
static size_t g_entry_count;
static int g_prefix_buckets[HASH_BUCKETS];
static int g_initialized;
static int g_enabled;
static FILE* g_trace;
static uint32_t g_trace_lines;
static subtitle_entry_t* find_entry(const char* source);
static void trace_source(const char* status, const char* source);

static void decode_escapes(char* text) {
    char* read = text;
    char* write = text;
    while (*read) {
        if (*read == '\\' && read[1]) {
            read++;
            if (*read == 'n') *write++ = '\n';
            else if (*read == 'r') *write++ = '\r';
            else if (*read == 't') *write++ = '\t';
            else *write++ = *read;
            read++;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static uint8_t rd8(uint8_t* rdram, uint32_t phys) {
    return rdram[phys ^ 3u];
}

static void wr8(uint8_t* rdram, uint32_t phys, uint8_t value) {
    rdram[phys ^ 3u] = value;
}

/* Despejo da tabela de glifos, para responder de forma empirica se da para
 * acentuar o texto.
 *
 * O formato veio de tools/wonder-source/src/code/code_8F1A0.c, func_80091A04:
 * o desenho de UM caractere le oito bytes consecutivos em `codigo * 8 + base`
 * e trata cada byte como uma linha de oito pixels, um bit por coluna. Ou seja
 * a fonte e um bitmap 1bpp de 8x8, oito bytes por glifo - nao um atlas.
 *
 * Sao dois bancos: D_8015F810 e D_8015F868 (este indexado a partir do codigo
 * 0x20, pois a rotina soma `- 0x100`). A escolha depende de D_8015B338 & 8,
 * que aparenta ser o realce do falante.
 *
 * Imprimir como arte ASCII permite conferir de olho que o enderecamento esta
 * certo - se 'A' sai com cara de A, a base e o passo estao corretos - e ver
 * quais faixas de codigo estao vazias e portanto livres para acentuadas. */
/* Um glifo esta "vazio" quando seus oito bytes sao zero. */
static int glifo_vazio(uint8_t* rdram, uint32_t base, unsigned codigo) {
    for (unsigned l = 0; l < 8u; l++)
        if (rd8(rdram, base + codigo * 8u + l)) return 0;
    return 1;
}

static void imprimir_glifo(uint8_t* rdram, uint32_t base, unsigned c) {
    printf("[fonte] glifo 0x%02X '%c'\n", c, (c >= 0x20u && c < 0x7Fu) ? (char)c : '?');
    for (unsigned linha = 0; linha < 8u; linha++) {
        uint8_t bits = rd8(rdram, base + c * 8u + linha);
        char arte[9];
        for (unsigned b = 0; b < 8u; b++)
            arte[b] = (bits & (0x80u >> b)) ? '#' : '.';
        arte[8] = '\0';
        printf("[fonte]   %s\n", arte);
    }
}

static unsigned bits_do_glifo(uint8_t* rdram, uint32_t base, unsigned c) {
    unsigned n = 0;
    for (unsigned l = 0; l < 8u; l++) {
        uint8_t b = rd8(rdram, base + c * 8u + l);
        while (b) { n += b & 1u; b >>= 1; }
    }
    return n;
}

/* Assinatura de uma tabela de fonte ASCII com 8 bytes por glifo.
 *
 * A primeira versao exigia apenas "espaco vazio e todas as letras com tinta",
 * e isso casou com dado aleatorio denso na area de boot - o 'A' saia como
 * ruido. Dado aleatorio passa em qualquer teste que so pergunte "tem bit
 * ligado?". O que dado aleatorio NAO faz e ficar todo zero em faixas inteiras
 * nem manter densidade de tinta plausivel.
 *
 * Daí os tres criterios abaixo. Os codigos de controle 0x00-0x1F e o espaco
 * precisam estar completamente zerados; todos os imprimiveis precisam ter
 * tinta; e cada glifo precisa ter entre 4 e 40 pixels acesos de 64 - uma letra
 * de verdade nunca e quase-vazia nem quase-cheia. */
static int parece_fonte(uint8_t* rdram, uint32_t base) {
    /* Nao exigir a faixa de controle vazia. A tabela real TEM desenhos em
       0x00..0x1F - blocos e caixas, visiveis na spritesheet - e a versao
       anterior deste teste, que os queria zerados, rejeitava a fonte
       verdadeira e devolvia zero tabelas. */
    if (!glifo_vazio(rdram, base, 0x20u)) return 0;          /* espaco vazio */
    /* Marcas finas sao o que separa fonte de framebuffer. Dado periodico ou
       ruidoso passa facil em "tem tinta"; o que ele nao faz e manter oito
       bytes somando dois ou tres bits acesos, do lado de letras cheias. */
    unsigned apostrofo = bits_do_glifo(rdram, base, 0x27u);
    unsigned ponto     = bits_do_glifo(rdram, base, 0x2Eu);
    if (apostrofo < 2u || apostrofo > 10u) return 0;
    if (ponto < 1u || ponto > 10u) return 0;
    for (unsigned c = 'A'; c <= 'Z'; c++) {
        unsigned n = bits_do_glifo(rdram, base, c);
        if (n < 6u || n > 40u) return 0;
    }
    for (unsigned c = 'a'; c <= 'z'; c++) {
        unsigned n = bits_do_glifo(rdram, base, c);
        if (n < 5u || n > 40u) return 0;
    }
    for (unsigned c = '0'; c <= '9'; c++) {
        unsigned n = bits_do_glifo(rdram, base, c);
        if (n < 6u || n > 40u) return 0;
    }
    return 1;
}

/* Exporta a tabela como spritesheet PGM de 128x128: 16 por 16 glifos de 8x8,
 * na ordem dos codigos. PGM porque e o formato mais simples que existe e o
 * projeto ja converte PPM/PGM para PNG em tools/ppm_para_png.py. */
static void exportar_spritesheet(uint8_t* rdram, uint32_t base, const char* caminho) {
    FILE* f = fopen(caminho, "wb");
    if (!f) return;
    fprintf(f, "P5\n128 128\n255\n");
    for (unsigned linha_glifo = 0; linha_glifo < 16u; linha_glifo++) {
        for (unsigned y = 0; y < 8u; y++) {
            for (unsigned coluna_glifo = 0; coluna_glifo < 16u; coluna_glifo++) {
                unsigned c = linha_glifo * 16u + coluna_glifo;
                uint8_t bits = rd8(rdram, base + c * 8u + y);
                for (unsigned x = 0; x < 8u; x++)
                    fputc((bits & (0x80u >> x)) ? 255 : 0, f);
            }
        }
    }
    fclose(f);
    printf("[fonte] spritesheet 128x128 gravada em %s\n", caminho);
}

void legendas_despejar_fonte(uint8_t* rdram) {
    static int feito = 0;
    const char* chave = getenv("WPJ2_DESPEJO_FONTE");
    if (feito || !chave || !*chave || *chave == '0') return;
    feito = 1;

    /* Seguir o codigo, nao adivinhar.
     *
     * Duas tentativas anteriores falharam e vale registrar as duas. A primeira
     * tratou D_8015F810 como se fosse a tabela; saiu lixo. A segunda varreu a
     * RDRAM procurando a assinatura de uma fonte, e casou com framebuffer -
     * qualquer teste que so pergunte "tem bit ligado?" passa em dado denso ou
     * periodico.
     *
     * A resposta estava na declaracao, em code_8F1A0.c:
     *
     *     extern u8* D_8015F810;   <- PONTEIRO
     *     extern u8* D_8015F868;
     *     extern u8* D_8015F870;
     *     extern u8* D_8015F878;
     *
     * Sao quatro ponteiros de fonte. O endereco da tabela e o valor GRAVADO
     * nesses enderecos, e nao os enderecos em si.
     *
     * Atencao ao indice: o banco de F868 e lido como `codigo*8 + ptr - 0x100`,
     * ou seja a tabela dele comeca no codigo 0x20 e nao em zero. */
    static const struct { uint32_t addr; int32_t vies; const char* nome; } pontos[] = {
        { 0x0015F810u,      0, "D_8015F810" },
        { 0x0015F868u, -0x100, "D_8015F868" },
        { 0x0015F870u,      0, "D_8015F870" },
        { 0x0015F878u,      0, "D_8015F878" },
    };
    unsigned achadas = 0;
    for (size_t p = 0; p < sizeof(pontos) / sizeof(pontos[0]); p++) {
        uint32_t a = pontos[p].addr;
        uint32_t valor = ((uint32_t)rd8(rdram, a) << 24) |
                         ((uint32_t)rd8(rdram, a + 1u) << 16) |
                         ((uint32_t)rd8(rdram, a + 2u) << 8) |
                          (uint32_t)rd8(rdram, a + 3u);
        printf("[fonte] %s -> 0x%08X\n", pontos[p].nome, valor);
        uint32_t base = (valor & 0x1FFFFFFFu) + (uint32_t)pontos[p].vies;
        if (!valor || base + 0x800u >= RDRAM_LIMIT) {
            printf("[fonte]   ponteiro fora da RDRAM ou nulo; pulando\n");
            continue;
        }
        achadas++;
        printf("[fonte] === tabela de %s em 0x%08X ===\n",
               pontos[p].nome, 0x80000000u | base);
        imprimir_glifo(rdram, base, 'A');
        imprimir_glifo(rdram, base, 'g');
        imprimir_glifo(rdram, base, '0');

        unsigned ocupados = 0;
        printf("[fonte] ocupacao por codigo (# usado, . livre):\n");
        for (unsigned linha_base = 0; linha_base < 256u; linha_base += 32u) {
            char linha[33];
            for (unsigned i = 0; i < 32u; i++) {
                unsigned c = linha_base + i;
                int usado = !glifo_vazio(rdram, base, c);
                linha[i] = usado ? '#' : '.';
                if (usado) ocupados++;
            }
            linha[32] = '\0';
            printf("[fonte]   0x%02X %s\n", linha_base, linha);
        }
        printf("[fonte] %u glifos com dado, %u livres de 256\n",
               ocupados, 256u - ocupados);
        char caminho[64];
        snprintf(caminho, sizeof(caminho), "temp\\fonte_%08X.pgm", base);
        exportar_spritesheet(rdram, base, caminho);
    }
    if (!achadas)
        printf("[fonte] nenhum ponteiro valido; a fonte pode nao ter sido "
               "carregada ainda neste instante da execucao\n");

    /* Varredura independente dos ponteiros.
     *
     * A caixa de dialogo continua desenhando o simbolo original mesmo com as
     * tres tabelas conhecidas compostas e recompostas a cada quadro. Ou existe
     * uma quarta tabela que nenhum dos quatro ponteiros aponta, ou o dialogo
     * usa outro sistema de fonte. Varrer a RDRAM inteira e mostrar, para cada
     * tabela encontrada, se o codigo 0x23 ja e o 'a' agudo separa as duas
     * hipoteses de uma vez: se aparecer uma tabela com '#' intacto, e ela que
     * o dialogo usa.
     *
     * O detector exige faixa de controle zerada, todos os imprimiveis com
     * tinta e densidade entre 4 e 40 pixels - a versao fraca de antes casava
     * com framebuffer. */
    printf("[fonte] --- varredura independente ---\n");
    unsigned varridas = 0;
    for (uint32_t b = 0; b + 0x800u < RDRAM_LIMIT && varridas < 8u; b += 4u) {
        if (!parece_fonte(rdram, b)) continue;
        varridas++;
        int composto = rd8(rdram, b + 0x23u * 8u + 0u) == rd8(rdram, b + 0x27u * 8u + 0u) &&
                       rd8(rdram, b + 0x23u * 8u + 1u) == rd8(rdram, b + 0x27u * 8u + 1u);
        printf("[fonte] tabela em 0x%08X  codigo 0x23 %s\n",
               0x80000000u | b, composto ? "JA COMPOSTO (a agudo)" : "ORIGINAL (#)");
    }
    printf("[fonte] %u tabela(s) na varredura\n", varridas);
    fflush(stdout);
}

/* ------------------------------------------------------------------
 * Acentuacao por composicao de glifos
 *
 * A fonte nao tem nenhuma letra acentuada, mas TEM os acentos soltos, porque
 * herdou o teclado ASCII do patch ingles. Compor a letra com o acento e melhor
 * do que desenhar glifos novos: o resultado usa exatamente os mesmos tracos, e
 * portanto casa com a fonte por construcao, sem ninguem ter que imitar o
 * estilo.
 *
 * Enderecos dos acentos, conferidos no despejo e nao presumidos - a contagem
 * visual erra por um por causa do ¥ japones na coluna 13:
 *
 *     0x27  '   agudo         0x5E  ^   circunflexo     0x7E  ~   til
 *
 * O aperto e vertical. Os acentos ocupam TRES linhas (0..2) e as minusculas so
 * deixam DUAS livres no topo: 'a', 'e', 'o', 'u' e 'c' comecam na linha 2.
 * Usamos entao as duas primeiras linhas do acento, que a 8x8 continuam
 * legiveis, e descartamos a terceira.
 *
 * O 'i' e o caso especial simpatico: a linha 0 e o pingo, que deve mesmo
 * desaparecer sob o acento. Sobrescrever as linhas 0..1 ja faz a coisa certa.
 *
 * O 'c' com cedilha precisa de espaco EMBAIXO, nao em cima. Como ele tem duas
 * linhas livres no topo, sobe-se o corpo em uma linha e sobram duas embaixo
 * para o gancho - bem melhor do que espremer a cedilha em uma linha so. */
#define ACENTO_AGUDO 0x27u
#define ACENTO_CIRC  0x5Eu
#define ACENTO_TIL   0x7Eu

static uint32_t g_fonte_base = 0;

static uint8_t fonte_ler(uint8_t* rdram, unsigned codigo, unsigned linha) {
    return rd8(rdram, g_fonte_base + codigo * 8u + linha);
}

static void fonte_gravar(uint8_t* rdram, unsigned codigo, const uint8_t* linhas) {
    for (unsigned l = 0; l < 8u; l++)
        wr8(rdram, g_fonte_base + codigo * 8u + l, linhas[l]);
}

/* Espelha um byte da esquerda para a direita: o grave e o agudo ao contrario,
   o que evita gastar um glifo de origem so para ele. */
static uint8_t espelhar(uint8_t b) {
    uint8_t r = 0;
    for (unsigned i = 0; i < 8u; i++) if (b & (1u << i)) r |= (uint8_t)(0x80u >> i);
    return r;
}

/* O acento chega COPIADO, nao pelo codigo de origem.
 *
 * Tem que ser assim porque varios destinos caem em cima dos proprios glifos de
 * origem - '^' e '~' viram letras acentuadas. Lendo o acento na hora, a
 * primeira composicao destruiria a fonte da segunda. Copiar as tres marcas
 * antes de escrever qualquer coisa elimina a dependencia de ordem. */
static void compor_acentuada(uint8_t* rdram, unsigned destino, unsigned base,
                             const uint8_t* acento, int espelhado) {
    uint8_t linhas[8];
    linhas[0] = acento[0];
    linhas[1] = acento[1];
    if (espelhado) { linhas[0] = espelhar(linhas[0]); linhas[1] = espelhar(linhas[1]); }
    for (unsigned l = 2; l < 8u; l++) linhas[l] = fonte_ler(rdram, base, l);
    fonte_gravar(rdram, destino, linhas);
}

static void compor_cedilha(uint8_t* rdram, unsigned destino, unsigned base) {
    uint8_t linhas[8];
    linhas[0] = 0;
    for (unsigned l = 1; l < 6u; l++) linhas[l] = fonte_ler(rdram, base, l + 1u);
    linhas[6] = 0x18u;   /* ...##... */
    linhas[7] = 0x30u;   /* ..##.... */
    fonte_gravar(rdram, destino, linhas);
}

/* Ordinais desenhados a mao, fora da faixa ASCII. Vieram do PBM editado e
   entram como dado literal porque nao ha de onde compo-los. */
static const uint8_t k_ordinal_masculino[8] =
    { 0x00, 0x60, 0x90, 0x90, 0x90, 0x60, 0x00, 0x00 };
static const uint8_t k_ordinal_feminino[8] =
    { 0x00, 0x60, 0x10, 0x70, 0x90, 0x68, 0x00, 0x00 };

/* Onde cada composta mora. Os codigos NAO sao livres a esmo - foram medidos.
 *
 * Duas tentativas erradas antes desta, e as duas ensinam algo.
 *
 * A primeira usou 0xF8..0xFF, que estavam vazios na tabela de glifos. Na tela
 * "vamos la!" virou "vamos l" mais uma marca solta, e o '!' DESAPARECEU. Um
 * caractere que leva embora o seguinte e a assinatura de codigo de controle
 * com argumento - o motor usa E0/E1/E2 assim, para pausa, variavel e cor do
 * falante. Glifo vazio na fonte nao significa codigo livre no motor.
 *
 * A segunda usou 0x5B..0x60 e 0x7B..0x7E, simbolos com glifo de verdade. Saiu
 * tudo em branco. Um despejo do texto provou que o byte chegava intacto ao
 * formatador, entao a recusa era do motor e nao da substituicao.
 *
 * Dai a sonda: trocar a cadeia por uma fileira de candidatos e olhar quais
 * aparecem. Resultado, em duas rodadas - o motor desenha aproximadamente
 * 0x20..0x5A e 0x61..0x7A, e trata o resto como espaco. Justamente por isso
 * '[', ']', '^', '_', '`', '{', '|', '}' e '~' nao servem: caem no vao entre
 * 'Z' e 'a'.
 *
 * Os catorze abaixo foram confirmados na tela, um por um. Sao simbolos que uma
 * traducao para portugues nao precisa - o que significa que o TSV NAO PODE
 * conte-los. Se algum dia um dialogo precisar de parenteses ou barra, e aqui
 * que se troca, e a sonda (WPJ2_SONDA_CODIGOS em runtime/hle.c) ainda esta la
 * para reconferir. */
#define COD_A_AGUDO  0x23u   /* era # */
#define COD_A_CIRC   0x24u   /* era $ */
#define COD_A_TIL    0x25u   /* era % */
#define COD_A_GRAVE  0x26u   /* era & */
#define COD_E_AGUDO  0x2Au   /* era * */
#define COD_E_CIRC   0x2Bu   /* era + */
#define COD_I_AGUDO  0x2Fu   /* era / */
#define COD_O_AGUDO  0x3Bu   /* era ; */
#define COD_O_CIRC   0x3Cu   /* era < */
#define COD_O_TIL    0x3Du   /* era = */
#define COD_U_AGUDO  0x3Eu   /* era > */
#define COD_C_CED    0x40u   /* era @ */
#define COD_ORD_MASC 0x28u   /* era ( */
#define COD_ORD_FEM  0x29u   /* era ) */

/* Compoe numa tabela. Devolve 1 se ela estava carregada e foi tratada. */
static int compor_numa_tabela(uint8_t* rdram, uint32_t base) {
    if (base + 0x800u >= RDRAM_LIMIT) return 0;
    /* Confere que a tabela ja esta carregada antes de escrever nela: sem isto
       gravariamos as compostas em cima de lixo e elas sumiriam no carregamento
       seguinte. Letras com tinta bastam como prova. */
    /* Exigir a assinatura COMPLETA de fonte, nao apenas "alguma letra tem
     * tinta".
     *
     * A guarda fraca custou caro e vale registrar. Os ponteiros D_8015F810 e
     * companhia nem sempre apontam para uma tabela de glifos - em alguns
     * instantes apontam para outro recurso. Qualquer buffer grafico passa em
     * "tem bit ligado", entao escreviamos as compostas por cima de imagem. O
     * teste de vandalismo (WPJ2_VANDALIZAR_FONTE) mostrou isso de forma
     * inequivoca: encher a letra 'a' nao alterou uma letra sequer na tela, mas
     * fez surgir um bloco ciano e revelou um "Progress" escondido.
     *
     * parece_fonte exige espaco vazio, apostrofo e ponto com poucos pixels, e
     * todas as letras e digitos com densidade plausivel. Um recurso grafico
     * nao satisfaz isso por acidente. */
    if (!parece_fonte(rdram, base)) return 0;
    g_fonte_base = base;

    /* Copia as tres marcas ANTES de escrever: varios destinos sao os proprios
       glifos de origem. Ver o comentario de compor_acentuada. */
    uint8_t agudo[2], circ[2], til[2];
    agudo[0] = fonte_ler(rdram, ACENTO_AGUDO, 0);
    agudo[1] = fonte_ler(rdram, ACENTO_AGUDO, 1);
    circ[0]  = fonte_ler(rdram, ACENTO_CIRC,  0);
    circ[1]  = fonte_ler(rdram, ACENTO_CIRC,  1);
    til[0]   = fonte_ler(rdram, ACENTO_TIL,   0);
    til[1]   = fonte_ler(rdram, ACENTO_TIL,   1);

    compor_acentuada(rdram, COD_A_AGUDO, 'a', agudo, 0);
    compor_acentuada(rdram, COD_A_CIRC,  'a', circ,  0);
    compor_acentuada(rdram, COD_A_TIL,   'a', til,   0);
    compor_acentuada(rdram, COD_A_GRAVE, 'a', agudo, 1);
    compor_acentuada(rdram, COD_E_AGUDO, 'e', agudo, 0);
    compor_acentuada(rdram, COD_E_CIRC,  'e', circ,  0);
    compor_acentuada(rdram, COD_I_AGUDO, 'i', agudo, 0);
    compor_acentuada(rdram, COD_O_AGUDO, 'o', agudo, 0);
    compor_acentuada(rdram, COD_O_CIRC,  'o', circ,  0);
    compor_acentuada(rdram, COD_O_TIL,   'o', til,   0);
    compor_acentuada(rdram, COD_U_AGUDO, 'u', agudo, 0);
    compor_cedilha  (rdram, COD_C_CED,   'c');
    fonte_gravar(rdram, COD_ORD_MASC, k_ordinal_masculino);
    fonte_gravar(rdram, COD_ORD_FEM,  k_ordinal_feminino);

    /* Teste de pertinencia, sob WPJ2_VANDALIZAR_FONTE=1.
     *
     * Depois de compor nas tres tabelas conhecidas, recompor a cada quadro e
     * ate recompor no instante exato do desenho, a caixa de dialogo continuava
     * mostrando o simbolo original. Isso deixa uma duvida que nenhuma medicao
     * indireta resolve: estas tabelas sao mesmo as que ela le?
     *
     * Encher a letra 'a' de tinta responde na hora e sem ambiguidade. Se
     * "vamos" aparecer com blocos no lugar dos 'a', a fonte e esta e o defeito
     * esta no codigo escolhido; se o texto sair intacto, a caixa desenha a
     * partir de outra fonte e toda a composicao esta no lugar errado. */
    if (getenv("WPJ2_VANDALIZAR_FONTE")) {
        static const uint8_t bloco[8] =
            { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        fonte_gravar(rdram, 'a', bloco);
    }
    return 1;
}

/* Compoe em TODAS as tabelas de fonte, nao so na primeira.
 *
 * A versao anterior tratava apenas D_8015F810 e o resultado foi um 'a' agudo
 * em branco na caixa de dialogo: o glifo estava certo na tabela que
 * compusemos, so que a caixa desenha a partir de outra. O jogo mantem quatro
 * ponteiros de fonte e o despejo achou tres tabelas distintas em memoria.
 *
 * Compor em todas e mais simples e mais seguro do que descobrir qual pertence
 * a qual elemento de tela: as tabelas sao pequenas, a operacao e idempotente e
 * nao existe caso em que acentuar uma fonte a mais atrapalhe.
 *
 * So termina quando todas as tabelas visiveis ja estavam carregadas - por isso
 * `feito` nao e marcado enquanto alguma ficar de fora. Uma fonte que so entra
 * na RDRAM ao trocar de cena seria perdida se paressemos na primeira passada. */
/* Catalogo de tabelas de fonte vistas ate agora.
 *
 * Os quatro ponteiros conhecidos nao bastam: o vandalismo provou que o texto
 * da caixa de dialogo nao sai de nenhum deles. Em vez de continuar procurando
 * qual ponteiro guarda a fonte certa, varre-se a RDRAM atras da assinatura e
 * compoe-se em TODA tabela encontrada. Nao e preciso saber qual e qual.
 *
 * A varredura e barata na pratica apesar de olhar 8 MB: parece_fonte comeca
 * exigindo o espaco (codigo 0x20) todo zerado, e esse teste de oito bytes
 * descarta quase todo candidato antes de ler mais qualquer coisa. */
#define MAX_TABELAS 12
static uint32_t g_tabelas[MAX_TABELAS];
static unsigned g_n_tabelas = 0;

static void registrar_tabela(uint32_t base) {
    for (unsigned i = 0; i < g_n_tabelas; i++)
        if (g_tabelas[i] == base) return;
    if (g_n_tabelas < MAX_TABELAS) {
        g_tabelas[g_n_tabelas++] = base;
        printf("[fonte] tabela catalogada em 0x%08X\n", 0x80000000u | base);
        fflush(stdout);
    }
}

/* Apenas RELATA. Nao registra, e portanto nao autoriza escrita em nada.
   Ver o comentario extenso no ponto de chamada. */
static void varrer_tabelas(uint8_t* rdram) {
    unsigned n = 0;
    for (uint32_t b = 0; b + 0x800u < RDRAM_LIMIT; b += 4u) {
        if (!parece_fonte(rdram, b)) continue;
        printf("[fonte] varredura: candidata em 0x%08X\n", 0x80000000u | b);
        n++;
        /* Pula a tabela inteira: sem isto o mesmo bloco casa varias vezes em
           deslocamentos de quatro bytes e a lista enche de aliases. */
        b += 0x7FCu;
    }
    printf("[fonte] varredura: %u candidata(s)\n", n);
    fflush(stdout);
}

/* Procura na RDRAM o desenho de uma letra recortada da TELA.
 *
 * Busca livre de atribuicao, e essa e a graca. Descobrir quem desenha o texto
 * falhou duas vezes: `ra` nao serve com fibers e trace_last_func() apontou um
 * mapeador de codigo como se fosse o desenhista. Mas o bitmap que aparece na
 * tela tem de existir em algum lugar da memoria, literalmente - entao procura-
 * se por ele, e o endereco encontrado E a fonte, sem intermediario.
 *
 * O padrao abaixo e o 'M' de "Menu", transcrito do framebuffer em (87,50),
 * oito linhas de oito pixels. Se aparecer com passo 8 entre glifos vizinhos a
 * fonte e 8x8; com passo 12, e a de doze linhas. */
/* Duas letras, de DUAS fontes diferentes - e essa distincao custou um ciclo.
 *
 * O 'M' veio de "Menu", no painel, e levou a tabela de 32 bytes em
 * 0x800E48B0. Compor ali nao mudou o texto da CAIXA DE DIALOGO, e encher o
 * slot 0x23 de tinta tambem nao: a caixa usa outra fonte. O 'O' abaixo foi
 * transcrito de "Ok," dentro da caixa, com o limiar invertido porque ali o
 * texto e claro sobre escuro. */
static const uint8_t k_padrao_M[8] =
    { 0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x44, 0x38 };   /* 'O' do dialogo */

/* Imagem do cartucho em memoria, guardada quando o patch de texto a recebe.
   A busca precisa dela porque a fonte pode estar na ROM e nao na RDRAM. */
static uint8_t* g_cart = NULL;
static size_t   g_cart_bytes = 0;

/* Desenho original do glifo que a acentuacao ocupa, guardado antes da primeira
   sobrescrita. Declarado aqui, e nao junto da composicao, porque a busca vem
   antes no arquivo e precisa dele. */
static uint8_t g_original_destino[12];
static int g_original_guardado = 0;

void legendas_procurar_glifo(uint8_t* rdram) {
    static int feito = 0;
    const char* chave = getenv("WPJ2_PROCURAR_GLIFO");
    if (feito || !chave || !*chave || *chave == '0') return;
    feito = 1;
    /* Cópias do glifo ORIGINAL que ainda estejam intactas.
     *
     * Com a nossa tabela ja patcheada, qualquer lugar da RDRAM que ainda tenha
     * o desenho original do codigo de destino e uma copia que nao recebeu a
     * composicao - e portanto candidata a ser a fonte que o jogo realmente le.
     *
     * Esta e a pergunta certa depois do impasse: a tela mostra o glifo
     * original, entao ele existe em algum lugar que nao e o nosso. Achar onde
     * vale mais do que continuar tentando escrever no lugar de sempre. */
    if (g_original_guardado) {
        printf("[glifo-busca] copias INTACTAS do glifo original:\n");
        unsigned copias = 0;
        for (uint32_t p = 0; p + 0x20u < RDRAM_LIMIT && copias < 12u; p += 2u) {
            unsigned l = 0;
            while (l < 12u && rd8(rdram, p + l * 2u) == g_original_destino[l]) l++;
            if (l < 12u) continue;
            copias++;
            printf("[glifo-busca]   0x%08X%s\n", 0x80000000u | p,
                   (p >= 0x000E48B0u + COD_A_AGUDO * 0x20u &&
                    p <= 0x000E48B0u + COD_A_AGUDO * 0x20u) ? "  (a nossa)" : "");
        }
        printf("[glifo-busca] %u copia(s) intacta(s)\n", copias);
        fflush(stdout);
    }

    printf("[glifo-busca] procurando o 'M' da tela na RDRAM...\n");
    unsigned achados = 0;
    /* Tentar deslocamentos de bit.
     *
     * A transcricao supoe que a celula do glifo comeca exatamente na coluna em
     * que a tinta comeca. Se ela comecar uma ou duas colunas antes, o mesmo
     * desenho aparece na memoria deslocado para a direita. Sem varrer os
     * deslocamentos, um erro de uma coluna na leitura da tela faz a busca
     * devolver zero e parecer que a fonte nao esta na RDRAM. */
    /* Varrer tambem o PASSO entre linhas, e nao so bytes contiguos.
     *
     * A busca contigua devolveu zero na RDRAM e no cartucho. Isso nao quer
     * dizer que o desenho nao esteja la: se a fonte for uma imagem de atlas,
     * as oito linhas do glifo ficam separadas pela largura da imagem. Um atlas
     * de 128 pixels em 1bpp da passo 16; de 256, passo 32. Varrer o passo cobre
     * qualquer largura ate 256 bytes sem precisar adivinhar. */
    for (unsigned desloc = 0; desloc < 4u && !achados; desloc++) {
    uint8_t alvo[8];
    for (unsigned i = 0; i < 8u; i++) alvo[i] = (uint8_t)(k_padrao_M[i] >> desloc);
    for (uint32_t p = 0; p + 8u < RDRAM_LIMIT && achados < 16u; p++) {
        if (rd8(rdram, p) != alvo[0]) continue;   /* poda barata */
        unsigned passo_ok = 0;
        for (unsigned passo = 1; passo <= 256u && !passo_ok; passo++) {
            if (p + 7u * passo >= RDRAM_LIMIT) break;
            unsigned k = 1;
            while (k < 8u && rd8(rdram, p + k * passo) == alvo[k]) k++;
            if (k == 8u) passo_ok = passo;
        }
        if (!passo_ok) continue;
        achados++;
        printf("[glifo-busca] deslocamento %u bit(s), passo %u byte(s)\n",
               desloc, passo_ok);
        printf("[glifo-busca] 'M' em 0x%08X\n", 0x80000000u | p);
        /* Com o passo conhecido, o tamanho de um glifo e 8*passo bytes. Se a
           tabela for indexada por codigo, a base sai de onde 'M' (0x4D) esta.
           Imprimir vizinhos confirma - ou desmente - essa suposicao. */
        uint32_t tam = 8u * passo_ok;
        uint32_t base = p - 0x4Du * tam;
        printf("[glifo-busca] base suposta 0x%08X (glifo de %u bytes)\n",
               0x80000000u | base, tam);
        /* Exporta a vizinhanca crua, para ler a ordem dos glifos como imagem.
         *
         * A suposicao de indice ASCII ja se mostrou falsa - no lugar de 'A'
         * aparece um 'G'. Sem a ordem real nao da para escolher o indice de
         * destino das acentuadas. Renderizar a regiao com 16 pixels de largura
         * (o passo medido) mostra a tabela inteira de uma vez, e a ordem se le
         * de olho. */
        {
            uint32_t ini = p > 0x2000u ? p - 0x2000u : 0u;
            FILE* f = fopen("temp\\fonte_regiao.bin", "wb");
            if (f) {
                for (uint32_t i = 0; i < 0x6000u && ini + i < RDRAM_LIMIT; i++)
                    fputc(rd8(rdram, ini + i), f);
                fclose(f);
                printf("[glifo-busca] regiao exportada de 0x%08X (24 KiB);"
                       " 'M' fica no byte 0x%X\n",
                       0x80000000u | ini, p - ini);
            }
        }
        static const unsigned prova[] = { 'A', 'B', 'a', '0' };
        for (unsigned q = 0; q < 4u; q++) {
            unsigned c = prova[q];
            printf("[glifo-busca]   '%c' (0x%02X):\n", (char)c, c);
            for (unsigned l = 0; l < 8u; l++) {
                uint8_t b = rd8(rdram, base + c * tam + l * passo_ok);
                char arte[9];
                for (unsigned x = 0; x < 8u; x++)
                    arte[x] = (b & (0x80u >> x)) ? '#' : '.';
                arte[8] = '\0';
                printf("[glifo-busca]     %s\n", arte);
            }
        }
    }
    }
    printf("[glifo-busca] %u ocorrencia(s) na RDRAM\n", achados);
    /* Procurar no cartucho SEMPRE, e nao so quando a RDRAM falha.
     *
     * A guarda antiga fazia sentido enquanto o cartucho era plano B. Deixou de
     * fazer: sabemos que a copia em RDRAM existe e que escrever nela nao muda
     * a tela, entao o que interessa agora e justamente saber se o desenho
     * tambem esta na ROM - porque patchear la resolve sem precisar descobrir
     * para onde o jogo copia. */
    {
        achados = 0;
        if (g_cart && g_cart_bytes > 8u) {
            for (unsigned desloc = 0; desloc < 4u && achados < 8u; desloc++) {
                uint8_t alvo[8];
                for (unsigned i = 0; i < 8u; i++)
                    alvo[i] = (uint8_t)(k_padrao_M[i] >> desloc);
                /* Com passo, igual a busca na RDRAM.
                 *
                 * A tentativa anterior procurou oito bytes contiguos no
                 * cartucho e nao achou nada - mas a fonte tem passo 2 entre
                 * linhas, entao contiguo nunca casaria. Se o desenho estiver
                 * na ROM, patchea-lo la resolve de uma vez: toda copia que o
                 * jogo fizer depois ja sai acentuada, e nao e preciso
                 * descobrir para onde ele copia. */
                for (size_t p = 0; p + 8u < g_cart_bytes && achados < 8u; p++) {
                    if (g_cart[p ^ 3u] != alvo[0]) continue;
                    for (unsigned passo = 1; passo <= 64u; passo++) {
                        if (p + 7u * passo >= g_cart_bytes) break;
                        unsigned k = 1;
                        while (k < 8u && g_cart[(p + k * passo) ^ 3u] == alvo[k]) k++;
                        if (k == 8u) {
                            achados++;
                            printf("[glifo-busca] no CARTUCHO em 0x%08X "
                                   "(desloc %u, passo %u)\n",
                                   (unsigned)p, desloc, passo);
                            break;
                        }
                    }
                }
            }
        }
        printf("[glifo-busca] %u ocorrencia(s) no cartucho\n", achados);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------
 * A FONTE DE VERDADE: celulas de 32 bytes, indexada por ASCII
 *
 * Achada pela busca livre de atribuicao - transcrever o 'M' de "Menu" do
 * framebuffer e procurar o desenho na memoria. O hexdump em volta revelou a
 * estrutura inteira:
 *
 *     800E5230  80 06  80 00 ... F8 00      'L', largura 6
 *     800E5250  82 08  C6 00 ... 92 00      'M', largura 8
 *     800E5270  84 07  C4 00 ... 84 00      'N', largura 7
 *
 * L, M e N consecutivos com passo 0x20 provam indice ASCII. Cada linha e um
 * u16 cujo byte ALTO e o bitmap; cabem 16 linhas na celula, e o 'g' usa dez
 * delas por causa da descendente. O byte 1 - que seria o byte baixo da linha
 * zero - guarda a LARGURA: a fonte e proporcional, nao monoespacada.
 *
 * Por que as tentativas anteriores falharam, resumido: a fonte 8x8 de
 * D_8015F810 existe mas nao desenha esta tela; func_80094230 nunca recebe
 * codigo de caractere; e uma busca por oito bytes contiguos jamais acharia
 * linhas separadas por dois bytes. */
#define F32_CEL   0x20u
#define F32_LINHAS 16u

static uint8_t f32_ler(uint8_t* rdram, uint32_t base, unsigned c, unsigned l) {
    return rd8(rdram, base + c * F32_CEL + l * 2u);
}
static void f32_escrever(uint8_t* rdram, uint32_t base, unsigned c,
                         unsigned l, uint8_t v) {
    wr8(rdram, base + c * F32_CEL + l * 2u, v);
}
static uint8_t f32_largura(uint8_t* rdram, uint32_t base, unsigned c) {
    return rd8(rdram, base + c * F32_CEL + 1u);
}
static void f32_def_largura(uint8_t* rdram, uint32_t base, unsigned c, uint8_t w) {
    wr8(rdram, base + c * F32_CEL + 1u, w);
}

/* Assinatura do 'A', usada para localizar a base sem depender de ponteiro.
   Dezesseis bytes muito especificos; a chance de coincidencia e desprezivel. */
static const uint8_t k_A32[8] = { 0x10, 0x28, 0x28, 0x28, 0x44, 0x7C, 0x82, 0x82 };

static int f32_confere(uint8_t* rdram, uint32_t base) {
    if (base + 0x100u * F32_CEL >= RDRAM_LIMIT) return 0;
    for (unsigned l = 0; l < 8u; l++)
        if (f32_ler(rdram, base, 'A', l) != k_A32[l]) return 0;
    return 1;
}

/* A base vem do PONTEIRO do jogo, nao de busca por assinatura.
 *
 * Isto encerra a caçada, e a resposta estava na referencia o tempo todo. Em
 * wonder-source, func_8009230C:
 *
 *     sp27 = *(arg0 * 0x20 + i * 2 + D_8015F874 - 0x400);
 *
 * Bate campo a campo com o que mediramos da tela: 32 bytes por glifo, linhas
 * em passo 2, doze linhas. O `- 0x400` e o detalhe que faltava - sao 32 celulas,
 * ou seja a tabela e indexada a partir do codigo 0x20.
 *
 * E sys_main.c explica por que havia varias copias e por que patchear a ROM
 * nao adiantou:
 *
 *     D_8015F880 = ...(gSeg_639B20_ROM_START, ...);
 *     D_8015F874 = SysMem_HeapAllocMark(...);
 *     Spi_DecompressAsset(D_8015F880, sp1DC, D_8015F874);
 *
 * A fonte esta COMPRIMIDA na ROM e e descomprimida para o heap. O que
 * encontravamos por assinatura eram copias dormentes; a viva e a que este
 * ponteiro aponta, e ela muda de lugar quando a cena recarrega - motivo pelo
 * qual o cache por assinatura travava numa copia que ninguem le. */
#define F32_PONTEIRO 0x0015F874u
#define F32_VIES     0x400u

static uint32_t f32_base_viva(uint8_t* rdram) {
    uint32_t v = ((uint32_t)rd8(rdram, F32_PONTEIRO) << 24) |
                 ((uint32_t)rd8(rdram, F32_PONTEIRO + 1u) << 16) |
                 ((uint32_t)rd8(rdram, F32_PONTEIRO + 2u) << 8) |
                  (uint32_t)rd8(rdram, F32_PONTEIRO + 3u);
    if (!v) return 0;
    uint32_t base = (v & 0x1FFFFFFFu);
    if (base < F32_VIES) return 0;
    base -= F32_VIES;
    if (base + 0x100u * F32_CEL >= RDRAM_LIMIT) return 0;
    return base;
}

static uint32_t f32_localizar_por_assinatura(uint8_t* rdram) {
    /* Cache com REVALIDACAO, nao cache cego.
     *
     * Guardar o endereco e escrever nele sem conferir ja corrompeu a tela uma
     * vez, quando o jogo reaproveitou a regiao. Aqui o endereco so e usado se
     * o 'A' ainda estiver la; caso contrario procura-se de novo. */
    static uint32_t base = 0;
    if (base && f32_confere(rdram, base)) return base;
    base = 0;
    for (uint32_t p = 0; p + 0x2000u < RDRAM_LIMIT; p += 2u) {
        if (rd8(rdram, p) != k_A32[0]) continue;
        unsigned l = 1;
        while (l < 8u && rd8(rdram, p + l * 2u) == k_A32[l]) l++;
        if (l < 8u) continue;
        if (p < 0x41u * F32_CEL) continue;
        uint32_t cand = p - 0x41u * F32_CEL;
        if (f32_confere(rdram, cand)) { base = cand; break; }
    }
    return base;
}

static void f32_compor_em(uint8_t* rdram, uint32_t base);

/* ---- Cerco do instante em que a acentuacao e desfeita ----
 *
 * Sabemos por medicao que a marca nao sobrevive de um quadro ao seguinte
 * (0 de 52800). O que NAO sabemos e onde, dentro do quadro, ela morre - e
 * enquanto isso for palpite, cada tentativa de conserto e um tiro no escuro.
 *
 * A ideia aqui e observar a marca em varios pontos ja instrumentados do
 * runtime e registrar em qual deles ela aparece intacta pela ultima vez. O
 * escritor esta necessariamente entre esse ponto e o seguinte. Nao adivinha
 * nada: mede.
 *
 * Tambem confere se a BASE mudou, porque "a marca sumiu" e "estamos olhando
 * outro endereco" produzem o mesmo sintoma e exigem consertos opostos. */
#define MARCA_VALOR 0x5Au

static uint32_t g_marca_base = 0;
static int g_marca_viva = 0;

void legendas_conferir_marca(uint8_t* rdram, const char* ponto) {
    /* Chave lida uma vez. Esta funcao chegou a ser chamada do plotador, que
       roda milhoes de vezes; um getenv por chamada ali derrubava a taxa. */
    static int ligado = -1;
    if (ligado < 0) ligado = getenv("WPJ2_CERCO_MARCA") != NULL;
    if (!ligado || !g_marca_base) return;
    /* A base ainda e a mesma? Um 'A' intacto no lugar esperado responde. */
    int base_ok = f32_confere(rdram, g_marca_base);
    int marca_ok = base_ok &&
        f32_ler(rdram, g_marca_base, COD_A_AGUDO, 0) == MARCA_VALOR;

    /* Contagem POR PONTO, nao um teto global de amostras.
     *
     * A versao anterior parava nas doze primeiras observacoes e todas cairam
     * em 'retrace-antes', de modo que os pontos que interessavam - o plotador,
     * sobretudo - nunca foram amostrados. Concluir dali seria supor de novo.
     *
     * Como os nomes sao literais de string, comparar o ponteiro basta para
     * identificar o ponto e sai de graca. */
    {
        #define PONTOS_MAX 8
        static const char* nomes[PONTOS_MAX];
        static uint64_t vistas[PONTOS_MAX], vivas[PONTOS_MAX];
        static unsigned n_pontos = 0;
        unsigned i;
        for (i = 0; i < n_pontos; i++) if (nomes[i] == ponto) break;
        if (i == n_pontos && n_pontos < PONTOS_MAX) {
            nomes[n_pontos] = ponto;
            vistas[n_pontos] = vivas[n_pontos] = 0;
            n_pontos++;
        }
        if (i < PONTOS_MAX) { vistas[i]++; if (marca_ok) vivas[i]++; }

        static unsigned periodo = 0;
        if ((periodo++ % 100000u) == 0u) {
            printf("[cerco] marca viva por ponto:");
            for (unsigned k = 0; k < n_pontos; k++)
                printf("  %s=%llu/%llu", nomes[k],
                       (unsigned long long)vivas[k],
                       (unsigned long long)vistas[k]);
            printf("\n");
            fflush(stdout);
        }
    }

    static const char* ultimo_vivo = "(nenhum)";
    static unsigned relatos = 0;
    if (g_marca_viva && !marca_ok) {
        g_marca_viva = 0;
        if (relatos++ < 20u) {
            printf("[cerco] marca morreu ENTRE '%s' e '%s'  (base %s)\n",
                   ultimo_vivo, ponto, base_ok ? "intacta" : "MUDOU");
            fflush(stdout);
        }
    } else if (marca_ok) {
        ultimo_vivo = ponto;
    }
}

/* Varre TODA a familia de ponteiros de fonte, em vez de escolher um.
 *
 * wonder-source mostra nove ponteiros vizinhos e pelo menos quatro rotinas de
 * desenho, com alturas e passos diferentes: func_80091A04 usa F810/F868 com
 * oito linhas, func_80094230 usa F880 com doze bytes, func_8009230C usa
 * F874-0x400 com celula de 0x20, e ha outra em F870 com dez linhas.
 *
 * Escolher um por vez foi o erro que custou este ciclo inteiro: cada tentativa
 * gastava uma execucao para descobrir que era o ponteiro errado. Compor em
 * todos os que passarem na assinatura resolve de uma vez e nao arrisca nada -
 * a assinatura do 'A' e forte, e ja foi ela que impediu a corrupcao quando o
 * cache cego escreveu em recurso alheio.
 *
 * O vies de 0x400 aparece no F874 porque a tabela dele comeca no codigo 0x20;
 * testamos com e sem, e usamos o que a assinatura aprovar. */
void compor_fonte32(uint8_t* rdram) {
    static const uint32_t familia[] = {
        0x0015F800u, 0x0015F808u, 0x0015F810u, 0x0015F868u, 0x0015F86Cu,
        0x0015F870u, 0x0015F874u, 0x0015F878u, 0x0015F880u
    };
    uint32_t feitas[12];
    unsigned n_feitas = 0;
    for (unsigned p = 0; p < sizeof(familia) / sizeof(familia[0]); p++) {
        uint32_t a = familia[p];
        uint32_t v = ((uint32_t)rd8(rdram, a) << 24) |
                     ((uint32_t)rd8(rdram, a + 1u) << 16) |
                     ((uint32_t)rd8(rdram, a + 2u) << 8) |
                      (uint32_t)rd8(rdram, a + 3u);
        if (!v) continue;
        uint32_t bruto = v & 0x1FFFFFFFu;
        for (unsigned k = 0; k < 2u; k++) {
            uint32_t cand = k ? (bruto >= F32_VIES ? bruto - F32_VIES : 0) : bruto;
            if (!cand || cand + 0x100u * F32_CEL >= RDRAM_LIMIT) continue;
            if (!f32_confere(rdram, cand)) continue;
            unsigned repetida = 0;
            for (unsigned j = 0; j < n_feitas; j++)
                if (feitas[j] == cand) repetida = 1;
            if (repetida) continue;
            if (n_feitas < 12u) feitas[n_feitas++] = cand;
            f32_compor_em(rdram, cand);
        }
    }
    /* Rede de seguranca, MAS espacada no tempo.
     *
     * f32_localizar_por_assinatura varre os 8 MB de RDRAM. Rodando a cada
     * quadro em que nenhum ponteiro valida - o que acontece sempre que a cena
     * troca e a fonte ainda nao esta posta - isso trava o jogo. Foi parte da
     * lentidao de cursor relatada apos a acentuacao.
     *
     * Uma tentativa a cada 300 chamadas mantem a rede util para o caso raro em
     * que os ponteiros falham, sem cobrar a varredura do quadro inteiro. */
    if (!n_feitas) {
        static unsigned espera = 0;
        if ((espera++ % 300u) == 0u) {
            uint32_t b = f32_localizar_por_assinatura(rdram);
            if (b) { feitas[n_feitas++] = b; f32_compor_em(rdram, b); }
        }
    }
    static unsigned relatado = 0;
    if (n_feitas && relatado != n_feitas) {
        relatado = n_feitas;
        printf("[fonte32] acentuacao em %u tabela(s):", n_feitas);
        for (unsigned j = 0; j < n_feitas; j++)
            printf(" 0x%08X", 0x80000000u | feitas[j]);
        printf("\n");
        fflush(stdout);
    }
}

static void f32_compor_em(uint8_t* rdram, uint32_t base) {
    if (!base) return;
    if (!g_original_guardado) {
        g_original_guardado = 1;
        for (unsigned l = 0; l < 12u; l++)
            g_original_destino[l] = f32_ler(rdram, base, COD_A_AGUDO, l);
    }

    /* Nossa escrita do quadro anterior sobreviveu?
     *
     * A busca a partir da tela achou UMA so ocorrencia do 'M', logo nao existe
     * segunda copia em 1bpp e o jogo rasteriza direto no framebuffer a partir
     * daqui. Mesmo assim vandalizar a letra 'a' nao muda nada na tela. So
     * restam duas explicacoes, e elas pedem consertos opostos: ou o jogo
     * sobrescreve a fonte entre a nossa escrita e o desenho, ou o texto e
     * desenhado uma vez e nao redesenhado.
     *
     * Contar quantas vezes o byte que gravamos ainda esta la no quadro
     * seguinte separa as duas sem ambiguidade. */
    if (getenv("WPJ2_MEDIR_SOBRESCRITA")) {
        static uint64_t verificacoes = 0, sobreviveu = 0;
        static int armado = 0;
        if (armado) {
            verificacoes++;
            if (f32_ler(rdram, base, COD_A_AGUDO, 0) == 0x5Au) sobreviveu++;
        }
        armado = 1;
        static unsigned periodo = 0;
        if ((periodo++ % 600u) == 0u) {
            printf("[sobrescrita] marca sobreviveu em %llu de %llu quadros\n",
                   (unsigned long long)sobreviveu,
                   (unsigned long long)verificacoes);
            fflush(stdout);
        }
        /* Marca reconhecivel, escrita depois da conferencia. */
        f32_escrever(rdram, base, COD_A_AGUDO, 0, 0x5Au);
    }

    /* Marcas ja alinhadas ao centro das minusculas da fonte Ryu.
     *
     * Copiar os glifos japoneses de apostrofo/circunflexo parecia mais fiel,
     * mas eles usam outra metrica horizontal: o agudo ficava no bit menos
     * significativo e aparecia sobre a letra SEGUINTE. O slot de til (0x7E)
     * na fonte inglesa e ainda pior: contem uma linha 0xFF, nao um til util.
     * Estes desenhos de duas linhas usam a mesma grade 8-pixel da fonte e
     * ficam centrados sobre as letras de largura 6. */
    static const uint8_t agudo[2] = { 0x10u, 0x20u };
    static const uint8_t grave[2] = { 0x40u, 0x20u };
    static const uint8_t circ [2] = { 0x20u, 0x50u };
    static const uint8_t til  [2] = { 0x50u, 0xA0u };

    struct { unsigned destino, letra; const uint8_t* ac; unsigned n; int esp; } tab[] = {
        { COD_A_AGUDO, 'a', agudo, 2, 0 },
        { COD_A_CIRC,  'a', circ,  2, 0 },
        { COD_A_TIL,   'a', til,   2, 0 },
        { COD_A_GRAVE, 'a', grave, 2, 0 },
        { COD_E_AGUDO, 'e', agudo, 2, 0 },
        { COD_E_CIRC,  'e', circ,  2, 0 },
        { COD_I_AGUDO, 'i', agudo, 2, 0 },
        { COD_O_AGUDO, 'o', agudo, 2, 0 },
        { COD_O_CIRC,  'o', circ,  2, 0 },
        { COD_O_TIL,   'o', til,   2, 0 },
        { COD_U_AGUDO, 'u', agudo, 2, 0 },
    };
    for (unsigned k = 0; k < sizeof(tab) / sizeof(tab[0]); k++) {
        if (!tab[k].n) continue;
        uint8_t linhas[F32_LINHAS];
        for (unsigned l = 0; l < F32_LINHAS; l++)
            linhas[l] = f32_ler(rdram, base, tab[k].letra, l);
        unsigned topo = 0;
        while (topo < F32_LINHAS && !linhas[topo]) topo++;
        if (tab[k].letra == 'i') {
            /* O ponto do i ocupa a linha zero e a linha 1 e vazia. Troca o
               ponto por uma diagonal curta, sem deslocar o corpo (linha 2). */
            linhas[0] = 0x40u;
            linhas[1] = 0x80u;
        } else {
            if (!topo) continue;
            unsigned usar = tab[k].n < topo ? tab[k].n : topo;
            for (unsigned i = 0; i < usar; i++)
                linhas[topo - usar + i] = tab[k].ac[tab[k].n - usar + i];
        }
        for (unsigned l = 0; l < F32_LINHAS; l++)
            f32_escrever(rdram, base, tab[k].destino, l, linhas[l]);
        f32_def_largura(rdram, base, tab[k].destino,
                        f32_largura(rdram, base, tab[k].letra));
    }

    /* Cedilha: o 'c' nao tem descendente, entao ha espaco logo abaixo dele. */
    {
        uint8_t linhas[F32_LINHAS];
        for (unsigned l = 0; l < F32_LINHAS; l++)
            linhas[l] = f32_ler(rdram, base, 'c', l);
        unsigned fim = F32_LINHAS;
        while (fim && !linhas[fim - 1]) fim--;
        if (fim + 1u < F32_LINHAS) {
            linhas[fim] = 0x10u;
            linhas[fim + 1u] = 0x30u;
            for (unsigned l = 0; l < F32_LINHAS; l++)
                f32_escrever(rdram, base, COD_C_CED, l, linhas[l]);
            f32_def_largura(rdram, base, COD_C_CED,
                            f32_largura(rdram, base, 'c'));
        }
    }

    /* Teste de pertinencia nesta fonte. O 'M' foi encontrado aqui por busca a
       partir da tela, entao ela E a fonte exibida; encher a letra 'a' separa
       "texto redesenhado a cada quadro" de "texto rasterizado uma vez". */
    if (getenv("WPJ2_VANDALIZAR_FONTE")) {
        /* Enche o SLOT DE DESTINO, nao a letra 'a'.
         *
         * O que se quer decidir e se o '#' que aparece em "vamos l#!" vem do
         * slot 0x23 desta tabela. Um bloco solido nesse slot responde sem
         * depender de transcrever pixel da tela: ou o '#' vira um bloco, e a
         * tabela e o slot estao certos, ou nao muda nada, e o caractere
         * desenhado vem de outro indice. */
        /* Alvo: o 'O' de "Ok," na caixa de dialogo.
         *
         * O 'O' exibido foi localizado por busca em 0x800E5290, que e
         * exatamente base + 'O'*0x20 - mesma tabela, indice ASCII. Se encher
         * esse slot mudar a tela, o texto E redesenhado a partir daqui e o
         * problema esta so no codigo 0x23. Se nao mudar, o texto foi
         * rasterizado antes e nao volta a ler a fonte. */
        /* Alvo: 'M', que aparece em "Menu" no painel.
         *
         * O 'O' so existe em "Ok," dentro da caixa de dialogo, entao encher
         * aquele slot testava apenas a caixa. O 'M' testa o painel. Se o
         * painel mudar e a caixa nao, esta provado que a fonte esta viva e que
         * a caixa e uma rasterizacao antiga. */
        /* Martelo na RDRAM: apaga a tabela INTEIRA.
         *
         * Equivalente ao que ja fizemos na ROM. Vandalizar um glifo por vez
         * sempre deu negativo, e a busca acabou de mostrar que nao ha nenhuma
         * copia intacta do glifo original em lugar nenhum da memoria. Se
         * apagar as 256 celulas nao alterar UM pixel de texto, entao esta
         * tabela nao alimenta texto algum nesta tela - e o casamento do 'M'
         * era uma copia dormente, nao a fonte viva. */
        for (unsigned c = 0x20u; c < 0x80u; c++)
            for (unsigned l = 0; l < 16u; l++)
                f32_escrever(rdram, base, c, l, 0xFFu);
    }

    /* Arma a marca por ultimo, para que qualquer escrita posterior a apague.
       O ponto de observacao seguinte que a vir morta delimita o intervalo. */
    if (getenv("WPJ2_CERCO_MARCA")) {
        g_marca_base = base;
        f32_escrever(rdram, base, COD_A_AGUDO, 0, MARCA_VALOR);
        g_marca_viva = 1;
    }

    static int relatado = 0;
    if (!relatado) {
        relatado = 1;
        printf("[fonte32] acentuacao aplicada em 0x%08X (celula 32B, ASCII)\n",
               0x80000000u | base);
        for (unsigned l = 0; l < 12u; l++) {
            uint8_t b = f32_ler(rdram, base, COD_A_AGUDO, l);
            char arte[9];
            for (unsigned x = 0; x < 8u; x++)
                arte[x] = (b & (0x80u >> x)) ? '#' : '.';
            arte[8] = '\0';
            printf("[fonte32]   %s\n", arte);
        }
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------
 * A fonte que o texto do jogo realmente usa: 8x12 em D_8015F880
 *
 * Toda a composicao anterior mirava a fonte 8x8 de D_8015F810. Ela existe e a
 * composicao funcionava nela, mas o texto exibido nunca mudou - o vandalismo
 * provou isso enchendo a letra 'a' sem alterar uma linha na tela.
 *
 * A resposta estava em wonder-source, func_80094230:
 *
 *     if (arg0 < 0xFF) {
 *         for (i = 0; i < 12; i++) {
 *             sp23 = *((arg0 * 6 * 2) + i + D_8015F880);
 *
 * Ou seja: 12 bytes por glifo, doze linhas de oito pixels, base em
 * D_8015F880, indexado pelo CODIGO DO CARACTERE - e nao pelos indices 0x2xx
 * que a nossa instrumentacao vinha registrando. Aqueles caem no `if` e nao
 * desenham nada, o que explica os doze bytes zerados que encontramos.
 *
 * Com doze linhas o aperto do 8x8 desaparece: sobra espaco de verdade para o
 * acento em cima e para a cedilha embaixo. */
#define F12_PTR 0x0015F880u
#define F12_ALT 12u

static uint8_t f12_ler(uint8_t* rdram, uint32_t base, unsigned c, unsigned l) {
    return rd8(rdram, base + c * F12_ALT + l);
}

static void f12_gravar(uint8_t* rdram, uint32_t base, unsigned c,
                       const uint8_t* linhas) {
    for (unsigned l = 0; l < F12_ALT; l++)
        wr8(rdram, base + c * F12_ALT + l, linhas[l]);
}

/* Copia as linhas com tinta de um glifo de acento. */
static unsigned f12_acento(uint8_t* rdram, uint32_t base, unsigned c,
                           uint8_t* saida) {
    unsigned n = 0;
    for (unsigned l = 0; l < F12_ALT; l++) {
        uint8_t b = f12_ler(rdram, base, c, l);
        if (b) saida[n++] = b;
        else if (n) break;
    }
    return n;
}

/* Encaixa o acento logo acima da letra, usando o espaco que ela deixa livre.
 *
 * Adaptativo de proposito: em vez de fixar "duas linhas", mede onde a letra
 * comeca e desce o acento ate encostar nela. Se nao couber inteiro, descarta
 * as linhas de cima do acento, que sao as menos caracteristicas. */
static void f12_compor(uint8_t* rdram, uint32_t base, unsigned destino,
                       unsigned letra, const uint8_t* acento, unsigned n_ac,
                       int espelhado) {
    uint8_t linhas[F12_ALT];
    for (unsigned l = 0; l < F12_ALT; l++)
        linhas[l] = f12_ler(rdram, base, letra, l);
    unsigned topo = 0;
    while (topo < F12_ALT && !linhas[topo]) topo++;
    if (!topo || topo >= F12_ALT) return;      /* sem folga em cima */
    unsigned usar = n_ac < topo ? n_ac : topo;
    for (unsigned k = 0; k < usar; k++) {
        uint8_t b = acento[n_ac - usar + k];
        linhas[topo - usar + k] = espelhado ? espelhar(b) : b;
    }
    f12_gravar(rdram, base, destino, linhas);
}

static void f12_cedilha(uint8_t* rdram, uint32_t base, unsigned destino,
                        unsigned letra) {
    uint8_t linhas[F12_ALT];
    for (unsigned l = 0; l < F12_ALT; l++)
        linhas[l] = f12_ler(rdram, base, letra, l);
    unsigned fim = F12_ALT;
    while (fim && !linhas[fim - 1]) fim--;
    if (fim + 1u < F12_ALT) { linhas[fim] = 0x18u; linhas[fim + 1u] = 0x30u; }
    else if (fim < F12_ALT)  { linhas[fim] = 0x18u; }
    else return;
    f12_gravar(rdram, base, destino, linhas);
}

/* Mesma cautela da fonte 8x8: so escrever onde a assinatura confirma fonte.
   Ver o historico de corrupcao mais acima neste arquivo. */
static int parece_fonte12(uint8_t* rdram, uint32_t base) {
    for (unsigned l = 0; l < F12_ALT; l++)
        if (f12_ler(rdram, base, 0x20u, l)) return 0;      /* espaco vazio */
    for (unsigned c = 'A'; c <= 'Z'; c++) {
        unsigned n = 0;
        for (unsigned l = 0; l < F12_ALT; l++) {
            uint8_t b = f12_ler(rdram, base, c, l);
            while (b) { n += b & 1u; b >>= 1; }
        }
        if (n < 6u || n > 60u) return 0;
    }
    for (unsigned c = 'a'; c <= 'z'; c++) {
        unsigned n = 0;
        for (unsigned l = 0; l < F12_ALT; l++) {
            uint8_t b = f12_ler(rdram, base, c, l);
            while (b) { n += b & 1u; b >>= 1; }
        }
        if (n < 5u || n > 60u) return 0;
    }
    return 1;
}

/* Codigo de destino das compostas? Serve para o compositor decidir, barato,
   se vale recompor antes de desenhar aquele glifo. */
int legendas_codigo_composto(unsigned codigo) {
    switch (codigo) {
        case COD_A_AGUDO: case COD_A_CIRC: case COD_A_TIL: case COD_A_GRAVE:
        case COD_E_AGUDO: case COD_E_CIRC: case COD_I_AGUDO:
        case COD_O_AGUDO: case COD_O_CIRC: case COD_O_TIL:
        case COD_U_AGUDO: case COD_C_CED:
        case COD_ORD_MASC: case COD_ORD_FEM:
            return 1;
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------
 * Fonte efetivamente usada pela ROM T-En do Ryu
 *
 * A ROM japonesa descrita por wonder-source e a ROM traduzida divergem neste
 * ponto. O formatador do patch ingles converte cada byte ASCII em EUC-JP,
 * depois Shift-JIS e finalmente num indice de objeto. Para dialogos, esses
 * indices sao truncados para dez bits (0x1xx/0x2xx) antes de chegar a
 * func_80094230, que le doze linhas em:
 *
 *   D_8015F880 + indice * 24 + linha * 2 - 0x1200
 *
 * Os bytes impares da celula sao preservados; o rasterizador usa os pares.
 * Os codigos PT-BR ocupam pontuacoes pouco usadas, logo seus objetos originais
 * eram precisamente os '#', '+', etc. vistos na captura final. */
#define OBJ_ALTURA 12u
#define OBJ_CELULA 24u
#define OBJ_VIES   0x1200u

typedef struct {
    uint16_t destino;
    uint16_t letra;
    uint8_t tipo; /* 0 agudo, 1 circunflexo, 2 til, 3 grave, 4 cedilha, 5 ord.m, 6 ord.f */
} glifo_ryu_t;

static const glifo_ryu_t g_glifos_ryu[] = {
    { 0x0193u, 0x023Cu, 0 }, /* # -> a agudo */
    { 0x018Fu, 0x023Cu, 1 }, /* $ -> a circunflexo */
    { 0x0192u, 0x023Cu, 2 }, /* % -> a til */
    { 0x0194u, 0x023Cu, 3 }, /* & -> a grave */
    { 0x0195u, 0x0240u, 0 }, /* * -> e agudo */
    { 0x017Bu, 0x0240u, 1 }, /* + -> e circunflexo */
    { 0x015Eu, 0x0244u, 0 }, /* / -> i agudo */
    { 0x0147u, 0x024Au, 0 }, /* ; -> o agudo */
    { 0x0182u, 0x024Au, 1 }, /* < -> o circunflexo */
    { 0x0180u, 0x024Au, 2 }, /* = -> o til */
    { 0x0183u, 0x0250u, 0 }, /* > -> u agudo */
    { 0x0196u, 0x023Eu, 4 }, /* @ -> c cedilha */
    { 0x0169u, 0x024Au, 5 }, /* ( -> ordinal masculino */
    { 0x016Au, 0x023Cu, 6 }, /* ) -> ordinal feminino */
};

static uint32_t objeto_ryu_endereco(uint32_t base, uint32_t indice) {
    return base + indice * OBJ_CELULA - OBJ_VIES;
}

static void objeto_ryu_ler(uint8_t* rdram, uint32_t endereco,
                           uint8_t linhas[OBJ_ALTURA]) {
    for (unsigned y = 0; y < OBJ_ALTURA; y++)
        linhas[y] = rd8(rdram, endereco + y * 2u);
}

static void objeto_ryu_gravar(uint8_t* rdram, uint32_t endereco,
                              const uint8_t linhas[OBJ_ALTURA]) {
    for (unsigned y = 0; y < OBJ_ALTURA; y++)
        wr8(rdram, endereco + y * 2u, linhas[y]);
}

static unsigned primeira_linha(const uint8_t linhas[OBJ_ALTURA]) {
    unsigned y = 0;
    while (y < OBJ_ALTURA && !linhas[y]) y++;
    return y;
}

static unsigned ultima_linha(const uint8_t linhas[OBJ_ALTURA]) {
    unsigned y = OBJ_ALTURA;
    while (y && !linhas[y - 1u]) y--;
    return y;
}

static void abrir_espaco_superior(uint8_t linhas[OBJ_ALTURA], unsigned precisa) {
    unsigned topo = primeira_linha(linhas);
    unsigned fim = ultima_linha(linhas);
    if (topo >= precisa || fim >= OBJ_ALTURA) return;
    unsigned descer = precisa - topo;
    if (fim + descer > OBJ_ALTURA) descer = OBJ_ALTURA - fim;
    if (!descer) return;
    for (unsigned y = fim; y-- > 0;) linhas[y + descer] = linhas[y];
    for (unsigned y = 0; y < descer; y++) linhas[y] = 0;
}

static void aplicar_marca_ryu(uint8_t linhas[OBJ_ALTURA], unsigned tipo) {
    static const uint8_t agudo[] = { 0x0Cu, 0x18u };
    static const uint8_t grave[] = { 0x30u, 0x18u };
    static const uint8_t circ[]  = { 0x18u, 0x24u };
    static const uint8_t til[]   = { 0x2Cu, 0x50u };
    const uint8_t* marca = agudo;
    unsigned n = 2u;

    if (tipo == 4u) {
        unsigned fim = ultima_linha(linhas);
        if (fim + 2u > OBJ_ALTURA) {
            for (unsigned y = 0; y + 2u < OBJ_ALTURA; y++) linhas[y] = linhas[y + 2u];
            linhas[OBJ_ALTURA - 2u] = linhas[OBJ_ALTURA - 1u] = 0;
            fim = ultima_linha(linhas);
        }
        if (fim < OBJ_ALTURA) linhas[fim] = 0x18u;
        if (fim + 1u < OBJ_ALTURA) linhas[fim + 1u] = 0x30u;
        return;
    }
    if (tipo == 1u) marca = circ;
    else if (tipo == 2u) marca = til;
    else if (tipo == 3u) marca = grave;

    /* O ponto do i e uma marca separada; remova-o antes de instalar o agudo. */
    unsigned topo = primeira_linha(linhas);
    if (topo + 1u < OBJ_ALTURA && linhas[topo] && !linhas[topo + 1u])
        linhas[topo] = 0;
    abrir_espaco_superior(linhas, n);
    topo = primeira_linha(linhas);
    if (topo < n) return;
    for (unsigned k = 0; k < n; k++) linhas[topo - n + k] = marca[k];
}

int legendas_compor_glifo_ryu(uint8_t* rdram, uint32_t indice_objeto) {
    const glifo_ryu_t* glifo = NULL;
    for (unsigned i = 0; i < sizeof(g_glifos_ryu) / sizeof(g_glifos_ryu[0]); i++) {
        if (g_glifos_ryu[i].destino == indice_objeto) {
            glifo = &g_glifos_ryu[i];
            break;
        }
    }
    if (!glifo) return 0;

    uint32_t base = ((uint32_t)rd8(rdram, F12_PTR) << 24) |
                    ((uint32_t)rd8(rdram, F12_PTR + 1u) << 16) |
                    ((uint32_t)rd8(rdram, F12_PTR + 2u) << 8) |
                     (uint32_t)rd8(rdram, F12_PTR + 3u);
    base &= 0x1FFFFFFFu;
    uint32_t origem = objeto_ryu_endereco(base, glifo->letra);
    uint32_t destino = objeto_ryu_endereco(base, glifo->destino);
    if (!base || origem + OBJ_CELULA > RDRAM_LIMIT ||
        destino + OBJ_CELULA > RDRAM_LIMIT) return 0;

    /* Preserve a segunda metade de cada par copiando a celula inteira. */
    for (unsigned i = 0; i < OBJ_CELULA; i++)
        wr8(rdram, destino + i, rd8(rdram, origem + i));

    uint8_t linhas[OBJ_ALTURA];
    objeto_ryu_ler(rdram, destino, linhas);
    if (glifo->tipo <= 4u) {
        aplicar_marca_ryu(linhas, glifo->tipo);
    } else {
        /* Ordinais pequenos, centrados na mesma metrica do banco. */
        memset(linhas, 0, sizeof(linhas));
        if (glifo->tipo == 5u) {
            linhas[1] = 0x18u; linhas[2] = 0x24u; linhas[3] = 0x24u;
            linhas[4] = 0x18u; linhas[5] = 0x3Cu;
        } else {
            linhas[1] = 0x18u; linhas[2] = 0x24u; linhas[3] = 0x1Cu;
            linhas[4] = 0x24u; linhas[5] = 0x1Eu;
        }
    }
    objeto_ryu_gravar(rdram, destino, linhas);

    static unsigned relatados = 0;
    if (relatados < 14u) {
        relatados++;
        printf("[fonte-ryu] objeto 0x%04X <- 0x%04X em 0x%08X\n",
               glifo->destino, glifo->letra, 0x80000000u | destino);
        fflush(stdout);
    }
    return 1;
}

void compor_fonte12(uint8_t* rdram);
void compor_fonte12(uint8_t* rdram) {
    uint32_t valor = ((uint32_t)rd8(rdram, F12_PTR) << 24) |
                     ((uint32_t)rd8(rdram, F12_PTR + 1u) << 16) |
                     ((uint32_t)rd8(rdram, F12_PTR + 2u) << 8) |
                      (uint32_t)rd8(rdram, F12_PTR + 3u);
    if (!valor) return;
    uint32_t base = valor & 0x1FFFFFFFu;
    if (base + 0x100u * F12_ALT >= RDRAM_LIMIT) return;
    if (!parece_fonte12(rdram, base)) return;

    uint8_t agudo[F12_ALT], circ[F12_ALT], til[F12_ALT];
    unsigned n_ag = f12_acento(rdram, base, ACENTO_AGUDO, agudo);
    unsigned n_ci = f12_acento(rdram, base, ACENTO_CIRC,  circ);
    unsigned n_ti = f12_acento(rdram, base, ACENTO_TIL,   til);
    if (!n_ag) return;

    f12_compor(rdram, base, COD_A_AGUDO, 'a', agudo, n_ag, 0);
    f12_compor(rdram, base, COD_A_CIRC,  'a', circ,  n_ci, 0);
    f12_compor(rdram, base, COD_A_TIL,   'a', til,   n_ti, 0);
    f12_compor(rdram, base, COD_A_GRAVE, 'a', agudo, n_ag, 1);
    f12_compor(rdram, base, COD_E_AGUDO, 'e', agudo, n_ag, 0);
    f12_compor(rdram, base, COD_E_CIRC,  'e', circ,  n_ci, 0);
    f12_compor(rdram, base, COD_I_AGUDO, 'i', agudo, n_ag, 0);
    f12_compor(rdram, base, COD_O_AGUDO, 'o', agudo, n_ag, 0);
    f12_compor(rdram, base, COD_O_CIRC,  'o', circ,  n_ci, 0);
    f12_compor(rdram, base, COD_O_TIL,   'o', til,   n_ti, 0);
    f12_compor(rdram, base, COD_U_AGUDO, 'u', agudo, n_ag, 0);
    f12_cedilha(rdram, base, COD_C_CED,  'c');

    static int relatado = 0;
    if (!relatado) {
        relatado = 1;
        printf("[fonte12] acentuacao aplicada em 0x%08X (8x12)\n",
               0x80000000u | base);
        for (unsigned l = 0; l < F12_ALT; l++) {
            uint8_t b = f12_ler(rdram, base, COD_A_AGUDO, l);
            char arte[9];
            for (unsigned x = 0; x < 8u; x++)
                arte[x] = (b & (0x80u >> x)) ? '#' : '.';
            arte[8] = '\0';
            printf("[fonte12]   %s\n", arte);
        }
        fflush(stdout);
    }
}

void legendas_compor_acentos(uint8_t* rdram) {
    /* Chave de desligamento, para poder comparar telas com e sem composicao.
     *
     * Sem ela nao ha como saber se uma tela estranha e corrupcao nossa ou uma
     * cena legitima do jogo: o quadro capturado sai no fim da execucao e esse
     * instante varia. Ja tirei conclusao errada por causa disso. */
    {
        static int ligado = -1;
        if (ligado < 0) {
            const char* e = getenv("WPJ2_ACENTOS_LEGADO");
            ligado = e && atoi(e) != 0;
        }
        if (!ligado) return;
    }
    /* Sem trava de "ja fiz".
     *
     * A versao anterior compunha uma vez so e o acento sumia ao entrar no
     * menu: o jogo recarrega a fonte por cena, e o DMA devolve o glifo
     * original por cima do nosso. Na tela isso aparecia como o simbolo
     * original de volta - "vamos l#!" em vez de "vamos la!".
     *
     * Recompor sempre e a resposta certa e nao a preguicosa: a operacao e
     * idempotente, le so letras e acentos que nunca sao destino, e escreve
     * 112 bytes por tabela. Detectar o instante do recarregamento custaria
     * mais e daria mais chance de errar. */
    static const uint32_t ponteiros[] = {
        0x0015F810u, 0x0015F868u, 0x0015F870u, 0x0015F878u
    };
    /* Esta e a fonte que o texto exibido usa; as outras ficam por seguranca. */
    compor_fonte32(rdram);
    compor_fonte12(rdram);

    unsigned tratadas = 0;
    for (size_t p = 0; p < 4u; p++) {
        uint32_t a = ponteiros[p];
        uint32_t valor = ((uint32_t)rd8(rdram, a) << 24) |
                         ((uint32_t)rd8(rdram, a + 1u) << 16) |
                         ((uint32_t)rd8(rdram, a + 2u) << 8) |
                          (uint32_t)rd8(rdram, a + 3u);
        if (!valor) continue;
        uint32_t base = valor & 0x1FFFFFFFu;
        /* O banco de F868 e indexado a partir do codigo 0x20 (a rotina de
           desenho soma -0x100), entao a tabela dele comeca antes do ponteiro. */
        if (p == 1u) base -= 0x100u;
        /* Compor JA, no endereco que o ponteiro tem AGORA. Nada de cache.
         *
         * Guardar os enderecos numa lista e recompor neles todo quadro parecia
         * inofensivo e corrompeu a tela: o jogo reaproveita aquela memoria
         * para outra coisa e nos continuavamos escrevendo la. parece_fonte nao
         * protege disso - dado reaproveitado as vezes passa na assinatura.
         *
         * Resolver o ponteiro a cada quadro nos amarra ao que o jogo diz ser
         * fonte naquele instante, que e a unica autoridade confiavel aqui. */
        if (compor_numa_tabela(rdram, base)) tratadas++;
    }

    /* A varredura NAO alimenta a composicao. Ela existe so como diagnostico,
     * sob WPJ2_VARRER_FONTES, e a razao e uma tentativa que deu errado.
     *
     * Alimentar o catalogo com o que a varredura encontra e compor em tudo
     * parecia elegante - dispensava descobrir qual tabela e de quem. Na
     * pratica a tela ficou preta. O detector, por mais criterioso que seja,
     * ainda casa com dado de recurso: as reincidencias em 0x802B62xx ficam na
     * vizinhanca da propria cadeia de dialogo, em 0x2B1F10. Escrever catorze
     * glifos ali destroi o que estava la.
     *
     * A licao vale alem daqui: heuristica boa o bastante para APONTAR nao e
     * boa o bastante para AUTORIZAR escrita. Enquanto a fonte do dialogo nao
     * for identificada por endereco de verdade - seguindo quem a le, e nao
     * adivinhando pelo conteudo - so se escreve onde um ponteiro do jogo
     * mandou. */
    if (getenv("WPJ2_VARRER_FONTES")) {
        static unsigned contador = 0;
        if ((contador++ % 120u) == 0u) varrer_tabelas(rdram);
    }

    static unsigned relatado = 0;
    if (tratadas && relatado != tratadas) {
        relatado = tratadas;
        printf("[fonte] acentuacao ativa em %u tabela(s)\n", tratadas);
        fflush(stdout);
    }
}

static char fold_utf8(const unsigned char* s, size_t* advance) {
    unsigned char lead = s[0];
    *advance = 1;
    if (lead < 0x80u) return (char)lead;
    if (lead == 0xC3u) {
        *advance = 2;
        switch (s[1]) {
            case 0x80: case 0x81: case 0x82: case 0x83: return 'A';
            case 0x87: return 'C';
            case 0x89: case 0x8A: return 'E';
            case 0x8D: return 'I';
            case 0x93: case 0x94: case 0x95: return 'O';
            case 0x9A: return 'U';
            /* Minusculas acentuadas viram os glifos compostos.
             *
             * As maiusculas continuam sendo rebaixadas para a letra simples, e
             * a razao e geometrica, nao preguica: 'A' ocupa da linha 0 a 6 e
             * nao sobra topo para o acento. Acentuar maiuscula exigiria
             * redesenhar a caixa alta inteira, uma linha mais baixa. */
            case 0xA0: return (char)(unsigned char)COD_A_GRAVE;  /* a` */
            case 0xA1: return (char)(unsigned char)COD_A_AGUDO;  /* a' */
            case 0xA2: return (char)(unsigned char)COD_A_CIRC;   /* a^ */
            case 0xA3: return (char)(unsigned char)COD_A_TIL;    /* a~ */
            case 0xA7: return (char)(unsigned char)COD_C_CED;    /* c, */
            case 0xA9: return (char)(unsigned char)COD_E_AGUDO;  /* e' */
            case 0xAA: return (char)(unsigned char)COD_E_CIRC;   /* e^ */
            case 0xAD: return (char)(unsigned char)COD_I_AGUDO;  /* i' */
            case 0xB3: return (char)(unsigned char)COD_O_AGUDO;  /* o' */
            case 0xB4: return (char)(unsigned char)COD_O_CIRC;   /* o^ */
            case 0xB5: return (char)(unsigned char)COD_O_TIL;    /* o~ */
            case 0xBA: return (char)(unsigned char)COD_U_AGUDO;  /* u' */
            default: return '?';
        }
    }
    /* Ordinais. Em UTF-8 sao 0xC2 0xBA e 0xC2 0xAA - bloco diferente das
       acentuadas, que ficam sob 0xC3, e por isso caiam no '?' ate agora. */
    if (lead == 0xC2u) {
        *advance = 2;
        if (s[1] == 0xBAu) return (char)(unsigned char)COD_ORD_MASC;   /* º */
        if (s[1] == 0xAAu) return (char)(unsigned char)COD_ORD_FEM;    /* ª */
        return '?';
    }
    if (lead == 0xE2u && s[1] == 0x80u) {
        *advance = 3;
        return s[2] == 0xA6u ? '.' : '-';
    }
    return '?';
}

static size_t make_ascii(char* out, size_t capacity, const char* text) {
    size_t in = 0, written = 0;
    if (!capacity) return 0;
    while (text[in] && written + 1u < capacity) {
        size_t advance;
        out[written++] = fold_utf8((const unsigned char*)text + in, &advance);
        in += advance;
    }
    out[written] = '\0';
    return written;
}

static int cart_text_has_format_collision(const char* text) {
    size_t in = 0;
    while (text[in]) {
        size_t advance;
        char folded = fold_utf8((const unsigned char*)text + in, &advance);
        /* COD_A_TIL e o proprio '%'. Recursos do cartucho podem atravessar
         * sprintf mais de uma vez; nem "%%" e estavel nesse caso, pois a
         * primeira passagem o reduz novamente a '%'. O interceptador de
         * recurso vivo roda depois dessa formatacao e e o lugar seguro. */
        if ((uint8_t)folded == COD_A_TIL &&
            (unsigned char)text[in] >= 0x80u)
            return 1;
        in += advance;
    }
    return 0;
}

static size_t make_fixed_ascii(char* out, size_t bytes, const char* text) {
    size_t written = make_ascii(out, bytes + 1u, text);
    while (written < bytes) out[written++] = ' ';
    out[bytes] = '\0';
    return written;
}

static uint32_t prefix_key_text(const char* text) {
    uint32_t key = 0;
    /* Cadeias curtas tambem sao chaves legitimas (Day -> Dia, Del ->
     * Apagar). A versao antiga lia sempre quatro bytes e depois as rejeitava;
     * alem de impedir a traducao, ler depois do NUL era indefinido para
     * entradas alocadas no tamanho exato. Complete a chave com zeros. */
    for (uint32_t i = 0; i < 4u && text[i]; i++)
        key |= (uint32_t)(uint8_t)text[i] << (i * 8u);
    return key;
}

static uint32_t prefix_key_cart(const uint8_t* cart, size_t pos) {
    return (uint32_t)cart[(pos + 0u) ^ 3u]
         | ((uint32_t)cart[(pos + 1u) ^ 3u] << 8u)
         | ((uint32_t)cart[(pos + 2u) ^ 3u] << 16u)
         | ((uint32_t)cart[(pos + 3u) ^ 3u] << 24u);
}

static void init_once(void) {
    if (g_initialized) return;
    g_initialized = 1;
    for (size_t i = 0; i < HASH_BUCKETS; i++) g_prefix_buckets[i] = -1;
    const char* configured = getenv("WPJ2_LEGENDAS");
    if (!configured || !*configured || !strcmp(configured, "0")) return;
    const char* path = !strcmp(configured, "1") ? "textos\\traducao_ptbr.tsv" : configured;
    FILE* file = fopen(path, "rb");
    if (!file) return;
    char line[2048];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return;
    }
    char* header_end = strpbrk(line, "\r\n");
    if (header_end) *header_end = '\0';
    if (strcmp(line, "source_en\tpt_br")) {
        fclose(file);
        return;
    }
    while (g_entry_count < MAX_ENTRIES && fgets(line, sizeof(line), file)) {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = '\0';
        char* end = strpbrk(tab, "\r\n");
        if (end) *end = '\0';
        decode_escapes(line);
        decode_escapes(tab);
        size_t source_len = strlen(line);
        if (!source_len || !*tab || source_len > MAX_TEXT) continue;
        subtitle_entry_t* entry = &g_entries[g_entry_count];
        entry->source = _strdup(line);
        entry->translated = _strdup(tab);
        if (!entry->source || !entry->translated) break;
        entry->source_len = (uint16_t)source_len;
        uint32_t bucket = prefix_key_text(entry->source) % HASH_BUCKETS;
        entry->next = g_prefix_buckets[bucket];
        g_prefix_buckets[bucket] = (int)g_entry_count;
        g_entry_count++;
    }
    fclose(file);
    g_enabled = g_entry_count != 0;
    const char* trace_path = getenv("WPJ2_LEGENDAS_LOG");
    if (trace_path && *trace_path) {
        g_trace = fopen(trace_path, "w");
        if (g_trace) fprintf(g_trace, "status\tsource_en\n");
    }
}

int legendas_substituir_recurso(uint8_t* rdram, uint32_t pointer) {
    init_once();
    if (!g_enabled) return 0;

    uint32_t phys = pointer & 0x1FFFFFFFu;
    if (phys >= RDRAM_LIMIT) return 0;

    char source[MAX_TEXT + 1];
    typedef struct {
        uint16_t source_offset;
        uint16_t translated_offset;
        uint8_t code, argument;
    } text_control_t;
    text_control_t controls[32];
    size_t source_n = 0, raw_n = 0, control_n = 0;
    int terminated = 0;
    while (raw_n < MAX_TEXT && phys + raw_n < RDRAM_LIMIT) {
        uint8_t value = rd8(rdram, phys + (uint32_t)raw_n++);
        if (!value) {
            terminated = 1;
            raw_n--;
            break;
        }
        /* O patch inglês usa E0/E1/E2 + argumento para pausa, variável e cor
         * do falante. Eles não pertencem à chave pesquisável, mas precisam
         * sobreviver à troca para o compositor manter o mesmo comportamento. */
        if (value >= 0xE0u && value <= 0xE2u &&
            raw_n < MAX_TEXT && phys + raw_n < RDRAM_LIMIT) {
            uint8_t arg = rd8(rdram, phys + (uint32_t)raw_n++);
            if (control_n < sizeof(controls) / sizeof(controls[0])) {
                controls[control_n].source_offset = (uint16_t)source_n;
                controls[control_n].code = value;
                controls[control_n].argument = arg;
                control_n++;
            }
            continue;
        }
        if (value == '\n' || value == '\r' ||
            (value >= 0x20u && value <= 0x7Eu)) {
            source[source_n++] = (char)value;
        } else {
            return 0;
        }
    }
    if (!terminated) return 0;
    while (source_n && (source[source_n - 1] == '\n' || source[source_n - 1] == '\r'))
        source_n--;
    source[source_n] = '\0';
    if (!source_n) return 0;

    subtitle_entry_t* entry = find_entry(source);
    if (!entry) return 0;

    char translated[SCRATCH_SLOT_BYTES];
    size_t translated_n = make_ascii(translated, sizeof(translated), entry->translated);
    size_t encoded_n = translated_n + control_n * 2u;

    /* Capacidade real, nao comprimento da cadeia inglesa.
     *
     * O limite antigo era `encoded_n > raw_n`, que trata o tamanho do texto
     * original como se fosse o tamanho do bloco. Nao e: o recurso quase sempre
     * termina com enchimento de zeros ate o alinhamento, e esses bytes estao
     * livres. Medir a folga em vez de presumi-la nao arrisca nada - so usamos
     * bytes que ja lemos como zero - e recupera traducoes que hoje ficam em
     * ingles nos DOIS caminhos.
     *
     * Vale insistir nisso porque a documentacao do projeto afirmava o
     * contrario. O comentario do patcher de cartucho diz que uma cadeia maior
     * "sera expandida no interceptador dinamico", mas o interceptador aplicava
     * exatamente a mesma restricao; na pratica nenhuma delas era traduzida em
     * lugar nenhum. textos/LEIA-ME.md contabiliza 694 recusadas por este
     * limite.
     *
     * A folga inclui o proprio terminador. Precisamos gravar caracteres em
     * 0..encoded_n-1 e o NUL em encoded_n, logo a condicao e
     * encoded_n <= raw_n + folga - 1. O teto evita que uma regiao grande de
     * zeros - que pode ser outra coisa, e nao enchimento - seja invadida. */
    #define FOLGA_MAX 96u
    size_t folga = 0;
    while (folga < FOLGA_MAX && phys + raw_n + folga < RDRAM_LIMIT &&
           rd8(rdram, phys + (uint32_t)(raw_n + folga)) == 0)
        folga++;
    size_t capacidade = folga ? raw_n + folga - 1u : raw_n;

    if (!translated_n || encoded_n > capacidade) {
        trace_source("recurso_ptbr_longo", source);
        return 0;
    }
    if (encoded_n > raw_n) {
        /* Marcado a parte para dar para medir o ganho no legendas_rota.tsv:
         * estas so cabem por causa do enchimento. */
        trace_source("recurso_ptbr_folga", source);
    }

    const char* source_colon = strchr(source, ':');
    const char* translated_colon = strchr(translated, ':');
    size_t source_colon_pos = source_colon ? (size_t)(source_colon - source) : source_n;
    size_t translated_colon_pos = translated_colon ?
        (size_t)(translated_colon - translated) : translated_n;
    for (size_t i = 0; i < control_n; i++) {
        size_t offset = controls[i].source_offset;
        size_t mapped;
        if (!offset) {
            mapped = 0;
        } else if (offset >= source_n) {
            mapped = translated_n;
        } else if (source_colon && translated_colon && offset <= source_colon_pos) {
            mapped = source_colon_pos ?
                (offset * translated_colon_pos) / source_colon_pos : 0;
        } else if (source_colon && translated_colon) {
            mapped = translated_colon_pos + (offset - source_colon_pos);
        } else {
            mapped = source_n ? (offset * translated_n) / source_n : 0;
        }
        if (mapped > translated_n) mapped = translated_n;
        controls[i].translated_offset = (uint16_t)mapped;
    }

    size_t out = 0;
    for (size_t pos = 0; pos <= translated_n; pos++) {
        for (size_t i = 0; i < control_n; i++) {
            if (controls[i].translated_offset == pos) {
                wr8(rdram, phys + (uint32_t)out++, controls[i].code);
                wr8(rdram, phys + (uint32_t)out++, controls[i].argument);
            }
        }
        if (pos < translated_n)
            wr8(rdram, phys + (uint32_t)out++, (uint8_t)translated[pos]);
    }
    /* Ate o MAIOR entre os dois, nao ate raw_n.
     *
     * Com raw_n sozinho, uma cadeia que usa a folga sai sem terminador: `out`
     * ja passou de raw_n quando o laco comeca e ele nao executa nenhuma vez.
     * O texto vazaria para o que viesse depois. Pelo maior, escreve-se o NUL
     * em `out` e, quando a traducao e mais curta, ainda se limpa o resto do
     * ingles original - que era o proposito inicial deste laco. */
    size_t limite_zeros = encoded_n > raw_n ? encoded_n : raw_n;
    while (out <= limite_zeros)
        wr8(rdram, phys + (uint32_t)out++, 0);
    trace_source("recurso_ptbr_nativo", source);
    return 1;
}

static void trace_source(const char* status, const char* source) {
    if (!g_trace || g_trace_lines++ >= 2048u) return;
    fprintf(g_trace, "%s\t", status);
    for (const unsigned char* p = (const unsigned char*)source; *p; p++) {
        if (*p == '\n') fputs("\\n", g_trace);
        else if (*p == '\r') fputs("\\r", g_trace);
        else if (*p == '\t') fputs("\\t", g_trace);
        else fputc(*p, g_trace);
    }
    fputc('\n', g_trace);
    fflush(g_trace);
}

static subtitle_entry_t* find_entry(const char* source) {
    if (!*source) return NULL;
    uint32_t bucket = prefix_key_text(source) % HASH_BUCKETS;
    for (int index = g_prefix_buckets[bucket]; index >= 0;
         index = g_entries[index].next)
        if (!strcmp(g_entries[index].source, source)) return &g_entries[index];
    return NULL;
}

/* Acentuacao aplicada NA ROM, antes de o jogo copiar a fonte.
 *
 * Este e o conserto certo, e o caminho ate ele foi longo. Compor na copia em
 * RDRAM nao funciona: a escrita persiste (marca viva em 100% de 2,8 milhoes de
 * observacoes, inclusive no plotador), a tabela e comprovadamente a certa - o
 * 'O' e o 'M' exibidos foram localizados nos slots ASCII exatos - e mesmo
 * assim encher um slot de tinta nao altera a tela. A unica leitura compativel
 * e que o jogo copia a fonte ao carregar a cena e desenha da copia.
 *
 * Em vez de caçar essa copia, patcheia-se a origem. A fonte esta na ROM em
 * 0x000E5E90 (o 'O', achado por busca com passo 2), e toda copia feita depois
 * ja sai acentuada.
 *
 * Mesmo formato da RDRAM: celula de 0x20, linha em passo 2, indice ASCII. */
static void compor_fonte_no_cartucho(uint8_t* cart, size_t rom_size) {
    static const uint8_t assinatura_O[8] =
        { 0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x44, 0x38 };
    size_t base = 0;
    for (size_t p = 0x4Fu * 0x20u; p + 0x10u < rom_size; p++) {
        if (cart[p ^ 3u] != assinatura_O[0]) continue;
        unsigned k = 1;
        while (k < 8u && cart[(p + k * 2u) ^ 3u] == assinatura_O[k]) k++;
        if (k == 8u) { base = p - 0x4Fu * 0x20u; break; }
    }
    if (!base) {
        printf("[fonte-rom] assinatura nao encontrada; acentuacao nao aplicada\n");
        return;
    }
    if (base + 0x100u * 0x20u >= rom_size) return;

    #define CLER(c, l)     cart[(base + (c) * 0x20u + (l) * 2u) ^ 3u]
    /* Mesmas marcas alinhadas usadas na copia viva em RDRAM. Nao usar os
       simbolos japoneses: as metricas horizontais deles deslocam o acento. */
    static const uint8_t ac[4][2] = {
        { 0x10u, 0x20u }, /* agudo */
        { 0x20u, 0x50u }, /* circunflexo */
        { 0x50u, 0xA0u }, /* til */
        { 0x40u, 0x20u }, /* grave */
    };

    static const struct { unsigned destino, letra, marca, esp; } tab[] = {
        { COD_A_AGUDO, 'a', 0, 0 }, { COD_A_CIRC, 'a', 1, 0 },
        { COD_A_TIL,   'a', 2, 0 }, { COD_A_GRAVE,'a', 3, 0 },
        { COD_E_AGUDO, 'e', 0, 0 }, { COD_E_CIRC, 'e', 1, 0 },
        { COD_I_AGUDO, 'i', 0, 0 }, { COD_O_AGUDO,'o', 0, 0 },
        { COD_O_CIRC,  'o', 1, 0 }, { COD_O_TIL,  'o', 2, 0 },
        { COD_U_AGUDO, 'u', 0, 0 },
    };
    unsigned feitos = 0;
    for (unsigned k = 0; k < sizeof(tab) / sizeof(tab[0]); k++) {
        unsigned m = tab[k].marca;
        uint8_t linhas[16];
        for (unsigned l = 0; l < 16u; l++) linhas[l] = CLER(tab[k].letra, l);
        unsigned topo = 0;
        while (topo < 16u && !linhas[topo]) topo++;
        if (tab[k].letra == 'i') {
            linhas[0] = 0x40u;
            linhas[1] = 0x80u;
        } else {
            if (!topo) continue;
            unsigned usar = topo < 2u ? topo : 2u;
            for (unsigned i = 0; i < usar; i++)
                linhas[topo - usar + i] = ac[m][2u - usar + i];
        }
        for (unsigned l = 0; l < 16u; l++) CLER(tab[k].destino, l) = linhas[l];
        /* Largura vive no byte 1 da celula; a acentuada ocupa o mesmo espaco. */
        cart[(base + tab[k].destino * 0x20u + 1u) ^ 3u] =
            cart[(base + tab[k].letra   * 0x20u + 1u) ^ 3u];
        feitos++;
    }
    /* Cedilha: o 'c' nao tem descendente, ha espaco logo abaixo. */
    {
        uint8_t linhas[16];
        for (unsigned l = 0; l < 16u; l++) linhas[l] = CLER('c', l);
        unsigned fim = 16u;
        while (fim && !linhas[fim - 1]) fim--;
        if (fim + 1u < 16u) {
            linhas[fim] = 0x10u; linhas[fim + 1u] = 0x30u;
            for (unsigned l = 0; l < 16u; l++) CLER(COD_C_CED, l) = linhas[l];
            cart[(base + COD_C_CED * 0x20u + 1u) ^ 3u] =
                cart[(base + 'c' * 0x20u + 1u) ^ 3u];
            feitos++;
        }
    }
    /* Ordinais desenhados a mao. Zera o restante da celula para nao herdar
       pixels dos parenteses que ocupavam estes codigos. */
    {
        static const struct {
            unsigned destino;
            const uint8_t* linhas;
            uint8_t largura;
        } ord[] = {
            { COD_ORD_MASC, k_ordinal_masculino, 5u },
            { COD_ORD_FEM,  k_ordinal_feminino,  5u },
        };
        for (unsigned q = 0; q < sizeof(ord) / sizeof(ord[0]); q++) {
            for (unsigned l = 0; l < 16u; l++)
                CLER(ord[q].destino, l) = l < 8u ? ord[q].linhas[l] : 0u;
            cart[(base + ord[q].destino * 0x20u + 1u) ^ 3u] = ord[q].largura;
            feitos++;
        }
    }
    /* Martelo: enche TODOS os imprimiveis de tinta.
     *
     * Nao e para consertar nada - e para testar a propria metodologia. Todos
     * os testes de vandalismo ate agora mexeram num glifo so e nao mudaram a
     * tela, o que ja seria estranho tendo a tabela sido localizada a partir
     * dos pixels exibidos. Se nem apagar a fonte inteira alterar a imagem,
     * entao o problema nao e a fonte: e o quadro que eu venho comparando, que
     * nao reflete o que este codigo faz. */
    if (getenv("WPJ2_ROM_FONTE_BLOCO")) {
        for (unsigned c = 0x20u; c < 0x80u; c++)
            for (unsigned l = 0; l < 16u; l++)
                cart[(base + c * 0x20u + l * 2u) ^ 3u] = 0xFFu;
        printf("[fonte-rom] MARTELO: 0x20..0x7F preenchidos de tinta\n");
    }
    #undef CLER
    printf("[fonte-rom] %u glifos acentuados gravados na ROM em 0x%08X\n",
           feitos, (unsigned)base);
    fflush(stdout);
}

void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size) {
    /* Guardar a imagem antes de qualquer coisa: a busca por glifo precisa dela
       e este e o unico ponto do runtime em que ela chega ate aqui. */
    g_cart = cart;
    g_cart_bytes = rom_size;
    init_once();
    if (!g_enabled) return;
    {
        const char* sem_cartucho = getenv("WPJ2_LEGENDAS_SEM_CARTUCHO");
        if (sem_cartucho && *sem_cartucho && *sem_cartucho != '0') return;
    }
    /* O compositor de ROM foi uma hipotese anterior e escreve numa fonte
     * dormente da T-En. Deixe-o apenas como controle diagnostico explicito;
     * a rota valida recompõe o objeto vivo em func_80094230. */
    if (getenv("WPJ2_ACENTOS_LEGADO") &&
        atoi(getenv("WPJ2_ACENTOS_LEGADO")) != 0)
        compor_fonte_no_cartucho(cart, rom_size);
    size_t entry_limit = g_entry_count;
    {
        const char* limite = getenv("WPJ2_CART_ENTRY_LIMIT");
        if (limite && *limite) {
            unsigned long n = strtoul(limite, NULL, 0);
            if (n < entry_limit) entry_limit = (size_t)n;
        }
    }
    unsigned patched = 0, skipped_long = 0;
    for (size_t pos = 0; pos + 4u <= rom_size; pos++) {
        uint32_t bucket = prefix_key_cart(cart, pos) % HASH_BUCKETS;
        for (int index = g_prefix_buckets[bucket]; index >= 0; index = g_entries[index].next) {
            if ((size_t)index >= entry_limit) continue;
            subtitle_entry_t* entry = &g_entries[index];
            size_t length = entry->source_len;
            /* Cadeias pequenas nao formam uma assinatura segura para varrer
             * uma ROM inteira. `End`/`Day` e outras palavras tambem aparecem
             * em estruturas binarias;
             * substituir cada coincidencia criou um ciclo no grafo consumido
             * por vsprintf e congelou a thread principal. Cadeias curtas
             * continuam validas no interceptador dinamico; no cartucho elas
             * exigem um endereco previamente confirmado. */
            if (length < 8u) continue;
            if (pos + length > rom_size) continue;
            size_t i = 0;
            while (i < length && cart[(pos + i) ^ 3u] == (uint8_t)entry->source[i]) i++;
            if (i != length) continue;
            /* A cadeia tem de terminar aqui - nao basta bater o prefixo.
             *
             * O menu guarda as opcoes num bloco contiguo separado por \n:
             *
             *     0x68B8E8 "Start\n"  "Delete Diary\n"  "Copy Diary\n"
             *     0x68B90C "Start without saving\n"
             *
             * Sem esta guarda, uma entrada curta como "Start" casaria tambem
             * no comeco de "Start without saving" e produziria "Jogar without
             * saving". O terminador aqui e \n ou \r, e nao so o NUL. */
            {
                uint8_t depois = (pos + length < rom_size)
                               ? cart[(pos + length) ^ 3u] : 0u;
                /* Rejeitar somente quando o byte seguinte for ASCII
                 * imprimivel, que e o unico caso em que a nossa cadeia e
                 * prefixo de algo maior.
                 *
                 * Listar terminadores aceitos seria frágil: alem de NUL e \n,
                 * o texto do jogo embute controles E0/E1/E2 para pausa,
                 * variavel e cor do falante, e uma fala pode terminar num
                 * deles. Uma lista curta rejeitaria essas e derrubaria
                 * traducoes que hoje funcionam. */
                if (depois >= 0x20u && depois < 0x7Fu) continue;
            }
            char fixed[MAX_TEXT + 1];
            char translated[MAX_TEXT + 1];
            if (cart_text_has_format_collision(entry->translated)) {
                skipped_long++;
                continue;
            }
            size_t translated_len = make_ascii(translated, sizeof(translated), entry->translated);
            /* Mede a folga em vez de presumir que o bloco tem o tamanho do
             * texto ingles - mesma correcao aplicada ao interceptador
             * dinamico, e pelo mesmo motivo.
             *
             * O comentario que estava aqui dizia que uma cadeia maior "sera
             * expandida no interceptador dinamico". Nao era verdade: o
             * interceptador tinha a mesma restricao, entao nenhum dos dois
             * caminhos traduzia essas cadeias. textos/LEIA-ME.md registra 694
             * recusadas por este limite.
             *
             * So se escreve sobre bytes lidos como zero, portanto o recurso
             * seguinte continua intocado. Precisamos de posicoes
             * 0..translated_len-1 mais o NUL, logo comparamos contra
             * length + folga - 1. */
            /* No cartucho, zero adjacente pode ser campo de uma estrutura e
             * nao preenchimento livre. A expansao sobre essa suposta folga
             * corrompeu objetos usados pelo formatador `%l%s`. Expansao fica
             * restrita ao recurso dinamico, onde a capacidade e observada
             * depois da descompressao. */
            size_t capacidade = length;
            if (translated_len > capacidade) {
                skipped_long++;
                continue;
            }
            /* make_fixed_ascii preenche exatamente `destino` bytes com o texto
             * e completa com zeros, entao passar a capacidade usada mantem o
             * terminador dentro da folga medida. */
            size_t destino = length;
            make_fixed_ascii(fixed, destino, entry->translated);
            for (i = 0; i < destino; i++) cart[(pos + i) ^ 3u] = (uint8_t)fixed[i];
            patched++;
        }
    }
    /* Excecoes curtas verificadas na ROM T-En. O `End` deste endereco pertence
     * a tela Message/Bird Speed; as outras ocorrencias ficam intocadas e podem
     * ser traduzidas quando chegarem como recurso textual vivo. */
    {
        static const struct { size_t offset; const char* source; } exact[] = {
            { 0x0068B8E8u, "Start" },
            { 0x0068B8F0u, "Delete Diary" },
            { 0x0068B900u, "Copy Diary" },
            { 0x0068B90Cu, "Start without saving" },
            { 0x0068BC70u, "Start without saving" },
            { 0x0068BED0u, "End" },
            { 0x0068BED8u, "Back" },
        };
        for (size_t e = 0; e < sizeof(exact) / sizeof(exact[0]); e++) {
            subtitle_entry_t* entry = find_entry(exact[e].source);
            size_t n = strlen(exact[e].source);
            if (!entry || exact[e].offset + n > rom_size) continue;
            size_t i = 0;
            while (i < n && cart[(exact[e].offset + i) ^ 3u] ==
                              (uint8_t)exact[e].source[i]) i++;
            if (i != n) continue;
            char translated[MAX_TEXT + 1];
            if (cart_text_has_format_collision(entry->translated)) continue;
            size_t translated_len = make_ascii(translated, sizeof(translated),
                                               entry->translated);
            if (!translated_len || translated_len > n) continue;
            for (i = 0; i < n; i++)
                cart[(exact[e].offset + i) ^ 3u] =
                    i < translated_len ? (uint8_t)translated[i] : (uint8_t)' ';
            patched++;
        }
    }
    if (g_trace && g_trace_lines++ < 2048u) {
        fprintf(g_trace, "cartucho_aplicado\t%u cadeias; %u nao cabem nem com folga\n",
                patched, skipped_long);
        fflush(g_trace);
    }
}

void legendas_capturar_rdram(uint8_t* rdram, const char* directory, unsigned id) {
    const char* enabled = getenv("WPJ2_LEGENDAS_RDRAM_CAPTURE");
    if (!enabled || !*enabled || *enabled == '0') return;
    char path[MAX_PATH + 80];
    snprintf(path, sizeof(path), "%s\\legendas_rdram_f5_%03u.tsv", directory, id);
    FILE* out = fopen(path, "w");
    if (!out) return;
    fprintf(out, "rdram_phys\ttexto_ascii\n");
    unsigned emitted = 0;
    for (uint32_t pos = 0; pos < RDRAM_LIMIT && emitted < 2048u;) {
        uint8_t first = rd8(rdram, pos);
        if (first < 0x20u || first > 0x7Eu) { pos++; continue; }
        char text[181];
        uint32_t length = 0, letters = 0;
        while (length < 180u && pos + length < RDRAM_LIMIT) {
            uint8_t value = rd8(rdram, pos + length);
            if (!value) break;
            if (value < 0x20u || value > 0x7Eu) { length = 0; break; }
            text[length++] = (char)value;
            if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) letters++;
        }
        if (length >= 4u && letters >= 3u && pos + length < RDRAM_LIMIT &&
            rd8(rdram, pos + length) == 0) {
            text[length] = '\0';
            fprintf(out, "0x%06X\t%s\n", pos, text);
            emitted++;
            pos += length + 1u;
        } else {
            pos++;
        }
    }
    fclose(out);

    /* A traducao inglesa armazena os dialogos em um fluxo codificado. O TSV
     * acima encontra apenas ASCII solto; a imagem completa permite localizar
     * o bloco decodificado e seu consumidor exatamente no instante do F5. */
    snprintf(path, sizeof(path), "%s\\legendas_rdram_f5_%03u.bin", directory, id);
    out = fopen(path, "wb");
    if (out) {
        uint8_t block[4096];
        for (uint32_t pos = 0; pos < RDRAM_DUMP_BYTES; pos += sizeof(block)) {
            for (uint32_t i = 0; i < sizeof(block); i++)
                block[i] = rd8(rdram, pos + i);
            fwrite(block, sizeof(block), 1, out);
        }
        fclose(out);
    }
}
