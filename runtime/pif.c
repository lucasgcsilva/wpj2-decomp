/* PIF minimo: o suficiente para o jogo enxergar um controle no canal 1.
 *
 * Interface lida em func_800CD4F0 (ROM 0xCE0F0), que e __osSiRawStartDma:
 *   $a0 direcao   0 -> escreve SI_PIF_ADDR_RD64B: PIF para RDRAM
 *                 1 -> escreve SI_PIF_ADDR_WR64B: RDRAM para PIF
 *   $a1 dramAddr  passa por osVirtualToPhysical e vai para SI_DRAM_ADDR
 *   $v0 retorno   0, ou -1 se __osSiDeviceBusy() nao for zero
 * O tamanho e sempre 0x40: a funcao invalida e escreve de volta exatamente 64
 * bytes de cache em torno de dramAddr.
 *
 * O bloco de 64 bytes e uma fita de comandos joybus. Cada comando tem o tamanho
 * de envio, o de resposta, o codigo e o espaco onde a resposta e escrita. Aqui
 * respondemos so o essencial: canal 0 e um controle padrao sem nenhum botao
 * pressionado e sem pak; os demais canais nao existem.
 *
 * Isto nao e emulacao de controle - e a ausencia de entrada, dita de forma que
 * o jogo entenda. Antes disso ele lia zeros crus, que nao sao uma resposta
 * valida do protocolo, e decidia com dado invalido.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "runtime.h"
#include "funcs.h"

#define PIF_SIZE 64

/* Botoes reportados ao jogo, no formato de 16 bits do controle do N64.
   Zero e "nada pressionado"; mudar por ambiente permite testar em paralelo a
   hipotese de o jogo estar esperando alguem apertar algo. */
/* volatile e obrigatorio: quem escreve e a thread de mensagens da janela
 * (video.c, WndProc) e quem le sao as fibers do jogo, noutra thread. Sem o
 * qualificador, o compilador com /O2 mantem o valor em registrador dentro do
 * laco de leitura e a escrita nunca fica visivel do outro lado.
 *
 * Foi exatamente o que se observou: o teclado registrava dezenas de eventos,
 * o jogo pedia leituras normalmente, e o PIF devolvia 0000 em todas. */
static volatile uint16_t g_buttons = 0;
/* Declaradas aqui porque pif_set_buttons vem antes delas no arquivo; a
 * definicao com inicializador fica mais abaixo, junto dos roteiros. */
static int g_roteiro_n;
static int g_roteiro_poll_n;

/* Entrada ao vivo tem prioridade sobre roteiro gravado.
 *
 * Sem isto, um WPJ2_INPUT/WPJ2_INPUT_POLLS deixado no ambiente fazia o
 * botoes_agora() ignorar g_buttons por completo, e nenhuma tecla chegava ao
 * jogo. Foi o que aconteceu na pratica: o teclado registrava dezenas de
 * eventos, o jogo pedia dez leituras, e todas devolviam 0000.
 *
 * Cancelar os roteiros na primeira tecla e o comportamento correto: quem esta
 * ao teclado quer dirigir, nao assistir a uma sequencia gravada. */
void pif_set_buttons(uint16_t b) {
    if (g_roteiro_n || g_roteiro_poll_n) {
        printf("entr : tecla pressionada; roteiro gravado cancelado\n");
        fflush(stdout);
        g_roteiro_n = 0;
        g_roteiro_poll_n = 0;
    }
    /* Prova do elo, nao suposicao. Tres tentativas de conserto falharam sem
     * que ninguem tivesse verificado que os dois lados falam da MESMA
     * variavel. Se o endereco impresso aqui for diferente do impresso em
     * botoes_agora(), existem duas copias e nenhum conserto de logica
     * funcionaria. */
    static unsigned avisos;
    if (avisos++ < 4) {
        printf("[elo] escrita: thread=%lu &g_buttons=%p valor=%04X\n",
               (unsigned long)GetCurrentThreadId(), (void*)&g_buttons, b);
        fflush(stdout);
    }
    g_buttons = b;
}

/* WPJ2_STICK aceita "x,y" (por exemplo "80,0"). O analogico antes ficava
 * sempre centrado, deixando sem teste uma entrada que o jogo pode usar para
 * navegar depois da tela inicial. */
static int8_t g_stick_x = 0, g_stick_y = 0;
void pif_set_stick(const char* value) {
    long x = 0, y = 0;
    char* end = NULL;
    if (value) {
        x = strtol(value, &end, 10);
        if (end && *end == ',') y = strtol(end + 1, &end, 10);
    }
    if (x < -128) x = -128; if (x > 127) x = 127;
    if (y < -128) y = -128; if (y > 127) y = 127;
    g_stick_x = (int8_t)x;
    g_stick_y = (int8_t)y;
    if (g_stick_x || g_stick_y)
        printf("entr : analogico %d,%d\n", g_stick_x, g_stick_y);
}

/* Roteiro temporal de entrada: "ms:botoes;ms:botoes;..."
 *
 * Segurar um botao nao e o mesmo que aperta-lo: a maioria dos jogos reage a
 * transicao, nao ao estado. Com um roteiro da para testar "aperta no segundo 3,
 * solta em 3,2" sem ninguem no teclado, e varios roteiros em paralelo. */
#define ROTEIRO_MAX 32
static struct { uint32_t ms; uint16_t botoes; } g_roteiro[ROTEIRO_MAX];
static int g_roteiro_n = 0;
static DWORD g_t0 = 0;

/* Roteiro deterministico, contado nas leituras CMD_READ_BTN que o proprio jogo
 * faz. O roteiro em milissegundos e util para uma pessoa, mas sob dezenas de
 * processos em paralelo o escalonador do host desloca uma janela de 50 ms. */
static struct { uint32_t poll; uint16_t botoes; } g_roteiro_poll[ROTEIRO_MAX];
static int g_roteiro_poll_n = 0;
static uint64_t g_polls_controle = 0;
static uint32_t g_polls_controle_logados = 0;

void pif_set_script(const char* s) {
    g_roteiro_n = 0;
    while (s && *s && g_roteiro_n < ROTEIRO_MAX) {
        char* fim = NULL;
        unsigned long ms = strtoul(s, &fim, 10);
        if (!fim || *fim != ':') break;
        unsigned long b = strtoul(fim + 1, &fim, 16);
        g_roteiro[g_roteiro_n].ms = (uint32_t)ms;
        g_roteiro[g_roteiro_n].botoes = (uint16_t)b;
        g_roteiro_n++;
        if (!fim || *fim != ';') break;
        s = fim + 1;
    }
    g_t0 = GetTickCount();
    printf("entr : roteiro com %d passo(s)\n", g_roteiro_n);
}

void pif_set_poll_script(const char* s) {
    g_roteiro_poll_n = 0;
    while (s && *s && g_roteiro_poll_n < ROTEIRO_MAX) {
        char* fim = NULL;
        unsigned long poll = strtoul(s, &fim, 10);
        if (!fim || *fim != ':') break;
        unsigned long b = strtoul(fim + 1, &fim, 16);
        g_roteiro_poll[g_roteiro_poll_n].poll = (uint32_t)poll;
        g_roteiro_poll[g_roteiro_poll_n].botoes = (uint16_t)b;
        g_roteiro_poll_n++;
        if (!fim || *fim != ';') break;
        s = fim + 1;
    }
    printf("entr : roteiro por leitura com %d passo(s)\n", g_roteiro_poll_n);
}

/* O estado corrente e o ultimo passo cujo instante ja passou. */
static uint16_t botoes_agora(void) {
    if (g_roteiro_poll_n) {
        uint16_t b = 0;
        for (int i = 0; i < g_roteiro_poll_n; i++)
            if (g_roteiro_poll[i].poll <= g_polls_controle) b = g_roteiro_poll[i].botoes;
        return b;
    }
    if (g_roteiro_n == 0) {
        /* O outro lado do elo. Comparar thread e endereco com o que
         * pif_set_buttons imprime responde de forma binaria se existe uma
         * variavel ou duas. */
        static unsigned avisos;
        if (avisos++ < 4) {
            printf("[elo] leitura: thread=%lu &g_buttons=%p valor=%04X\n",
                   (unsigned long)GetCurrentThreadId(), (void*)&g_buttons,
                   (unsigned)g_buttons);
            fflush(stdout);
        }
        return g_buttons;
    }
    uint32_t t = (uint32_t)(GetTickCount() - g_t0);
    uint16_t b = 0;
    for (int i = 0; i < g_roteiro_n; i++) {
        if (g_roteiro[i].ms <= t) b = g_roteiro[i].botoes;
    }
    return b;
}

static uint8_t g_pif[PIF_SIZE];
static uint64_t g_si_reads = 0;
static uint64_t g_si_writes = 0;
static uint64_t g_pif_cmds = 0;
/* Cada __osSiRawStartDma termina uma transferencia. O hardware levanta SI
 * para essa transferencia, e nao uma vez por retrace. Manter o contador evita
 * perder uma conclusao caso duas operacoes sejam iniciadas antes do proximo
 * ponto de entrega do HLE. */
static uint32_t g_si_done = 0;

uint64_t pif_si_reads(void)  { return g_si_reads; }
uint64_t pif_si_writes(void) { return g_si_writes; }
uint64_t pif_commands(void)  { return g_pif_cmds; }
uint64_t pif_controller_polls(void) { return g_polls_controle; }
int pif_si_done_pending(void) { return g_si_done != 0; }
void pif_take_si_done(void) { if (g_si_done) g_si_done--; }

/* Relatorio periodico, disparado pelo relogio e nao por uma leitura.
 *
 * Motivo: o log por leitura emudeceu aos 2,3 s e nao havia como separar duas
 * causas muito diferentes - "o jogo parou de perguntar pelo controle" e "o
 * roteiro nunca chegou a produzir um botao". Um relatorio preso a leitura
 * nunca responde isso, porque some junto com o sintoma que se quer medir.
 *
 * Este sai a cada segundo enquanto o processo viver e imprime lado a lado: o
 * total de leituras (para achar o instante exato em que estagna), os dois
 * contadores de SI, as conclusoes ainda nao entregues - se este numero cresce
 * sem parar, post_event esta falhando e a thread do controle ficou bloqueada
 * esperando uma mensagem que nunca chega - e o valor que o roteiro entregaria
 * AGORA, independente de existir alguem para le-lo. */
void pif_relatorio_periodico(void) {
    static DWORD base = 0, proximo = 0;
    DWORD agora = GetTickCount();
    if (base == 0) { base = agora; proximo = agora; }
    if ((long)(agora - proximo) < 0) return;
    proximo = agora + 1000;
    printf("[pif-per] t=%lu ms leituras=%llu si_w=%llu si_r=%llu pendentes=%u "
           "roteiro=%04X ao_vivo=%04X\n",
           (unsigned long)(agora - base),
           (unsigned long long)g_polls_controle,
           (unsigned long long)g_si_writes,
           (unsigned long long)g_si_reads,
           (unsigned)g_si_done,
           (unsigned)botoes_agora(), (unsigned)g_buttons);
    fflush(stdout);
}

/* A RDRAM guarda cada palavra de 32 bits na ordem de bytes do host; o bloco do
   PIF e uma sequencia de bytes. Trocar de quatro em quatro converte entre as
   duas visoes, e a operacao e a sua propria inversa. */
static void copy_bswap(uint8_t* dst, const uint8_t* src, size_t bytes) {
    for (size_t i = 0; i + 3 < bytes; i += 4) {
        dst[i + 0] = src[i + 3];
        dst[i + 1] = src[i + 2];
        dst[i + 2] = src[i + 1];
        dst[i + 3] = src[i + 0];
    }
}

/* Codigos joybus usados aqui. */
#define CMD_INFO        0x00   /* identidade do dispositivo */
#define CMD_READ_BTN    0x01   /* estado dos botoes e do analogico */
#define CMD_RESET       0xFF   /* reset do canal; responde como INFO */

#define CH_CONNECTED    0      /* so o canal 0 tem alguma coisa ligada */
#define NO_DEVICE       0x80   /* bit posto no tamanho de resposta */

/* Percorre a fita de comandos e preenche as respostas no lugar. */
static void pif_process(void) {
    int i = 0, channel = 0;

    while (i < PIF_SIZE) {
        uint8_t t = g_pif[i];

        if (t == 0x00) { i++; channel++; continue; }   /* pula o canal      */
        if (t == 0xFD) { i++; continue; }              /* reset do canal    */
        if (t == 0xFE) break;                          /* fim dos comandos  */
        if (t == 0xFF) { i++; continue; }              /* enchimento        */

        int tx = t & 0x3F;
        if (i + 1 >= PIF_SIZE) break;
        int rx = g_pif[i + 1] & 0x3F;
        if (tx == 0 || i + 2 + tx + rx > PIF_SIZE) break;

        uint8_t cmd = g_pif[i + 2];
        uint8_t* out = &g_pif[i + 2 + tx];
        g_pif_cmds++;

        if (channel != CH_CONNECTED) {
            /* Nada ligado: o protocolo pede o bit 0x80 no tamanho de resposta. */
            g_pif[i + 1] |= NO_DEVICE;
        } else if (cmd == CMD_INFO || cmd == CMD_RESET) {
            if (rx >= 3) {
                out[0] = 0x05;   /* tipo: controle padrao */
                out[1] = 0x00;
                /* 0x02 e CONT_CARD_PULL (remocao recente), nao "sem pak".
                 * Reporta zero para um controle conectado e sem acessorio;
                 * assim a libultra nao reinicia a deteccao de PFS a cada
                 * consulta de estado. */
                out[2] = 0x00;
            }
        } else if (cmd == CMD_READ_BTN) {
            if (rx >= 4) {
                g_polls_controle++;
                uint16_t b = botoes_agora();
                /* Registrar por MUDANCA, nao pelas primeiras N leituras. O
                 * limite antigo se esgotava no boot e nada aparecia quando a
                 * pessoa apertava uma tecla no titulo, minutos depois - que e
                 * justamente o instante que interessa. */
                static uint16_t ultimo = 0;
                static int primeira = 1;
                if (b != ultimo || primeira) {
                    primeira = 0;
                    printf("[pif] MUDOU botoes=%04X na leitura %llu\n",
                           b, (unsigned long long)g_polls_controle);
                    fflush(stdout);
                    ultimo = b;
                }
                if (g_polls_controle_logados++ < 32) {
                    printf("[pif] leitura=%llu tempo=%lu ms botoes=%04X\n",
                           (unsigned long long)g_polls_controle,
                           (unsigned long)(GetTickCount() - g_t0), b);
                    fflush(stdout);
                }
                out[0] = (uint8_t)(b >> 8);
                out[1] = (uint8_t)(b & 0xFF);
                out[2] = (uint8_t)g_stick_x;
                out[3] = (uint8_t)g_stick_y;
            }
        } else {
            /* Pak de memoria e o resto: respondido como ausente, em vez de
               devolver bytes inventados que o jogo trataria como validos. */
            g_pif[i + 1] |= NO_DEVICE;
        }

        i += 2 + tx + rx;
        channel++;
    }
}

/* __osSiRawStartDma(s32 dir, void* dramAddr) */
void func_800CD4F0(uint8_t* rdram, recomp_context* ctx) {
    int32_t  dir  = (int32_t)ctx->r4;
    uint32_t dram = (uint32_t)ctx->r5 & 0x1FFFFFFFu;

    /* Quem chama esta funcao. osContStartReadData monta a fita e chama
     * __osSiRawStartDma; identificar o chamador por observacao evita procurar
     * a funcao no desmontado. O endereco de retorno esta em ra (r31). */
    {
        static uint32_t vistos[16];
        static unsigned n;
        uint32_t ra = (uint32_t)ctx->r31;
        /* A chave inclui a direcao. Deduplicar so pelo ra escondia metade dos
         * chamadores: quando a chamada com dir=0 retorna para o mesmo
         * endereco da com dir=1, ela nunca era registrada - e e justamente o
         * par das duas direcoes que separa osContStartReadData (monta a fita)
         * de osContGetReadData (le o resultado). */
        uint32_t chave = ra ^ ((uint32_t)dir << 28);
        unsigned i;
        for (i = 0; i < n; i++) if (vistos[i] == chave) break;
        if (i == n && n < 16) {
            vistos[n++] = chave;
            printf("[chamador] __osSiRawStartDma dir=%d ra=0x%08X\n", dir, ra);
            fflush(stdout);
        }
    }

    if (dir == 1) {
        /* RDRAM -> PIF: o jogo acabou de montar a fita de comandos. */
        copy_bswap(g_pif, rdram + dram, PIF_SIZE);
        pif_process();
        g_si_writes++;
    } else {
        /* PIF -> RDRAM: devolve a fita ja com as respostas.
         *
         * Reexecutar a fita AQUI nao e redundancia - e o comportamento do
         * hardware, e sem isto o controle congela. A libultra escreve a fita
         * uma unica vez; veja tools/libreultra/src/io/contreaddata.c:
         *
         *     if (__osContLastCmd != CONT_CMD_READ_BUTTON) {
         *         __osPackReadData();
         *         __osSiRawStartDma(OS_WRITE, &__osContPifRam);   // 1a vez so
         *         osRecvMesg(mq, NULL, OS_MESG_BLOCK);
         *     }
         *     __osSiRawStartDma(OS_READ, &__osContPifRam);        // toda vez
         *     __osContLastCmd = CONT_CMD_READ_BUTTON;
         *
         * A PIF RAM real retem a fita e o PIF a reexecuta a cada leitura, de
         * modo que o OS_READ sozinho ja traz botoes novos. Processando so em
         * dir=1, nos devolviamos para sempre o retrato tirado na ultima
         * escrita. Medido antes da correcao, com relatorio de um em um
         * segundo: si_w parou em 29 aos 2,0 s e nunca mais subiu, enquanto
         * si_r seguiu em ~30/s ate 1176 aos 40 s - o jogo lia 30 vezes por
         * segundo uma resposta de dois segundos atras. Era por isso que
         * segurar START desde o boot "funcionava" (caia dentro da janela das
         * 10 leituras iniciais) e apertar no titulo nunca funcionava. */
        pif_process();
        copy_bswap(rdram + dram, g_pif, PIF_SIZE);
        g_si_reads++;
        /* Onde os botoes entram na memoria do jogo.
         *
         * Ja esta verificado que o PIF entrega o valor certo em toda leitura,
         * que o formato joybus esta correto e que nada no caminho e stub. O que
         * falta saber e o que a ROM faz com o dado depois. Registrar o endereco
         * de destino permite coloca-lo sob watch e responder de forma binaria:
         * se o valor aparecer aqui e o titulo nao reagir, o defeito esta na
         * logica do jogo; se nao aparecer, esta no parse da fita.
         *
         * Limitado as primeiras ocorrencias para nao inundar o log. */
        static unsigned relatados;
        if (relatados++ < 8) {
            /* A fita ja esta na RDRAM; localizamos a resposta do canal 0
             * relendo do mesmo bloco que acabamos de escrever. */
            printf("[pif] destino=0x%06X fita=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                   dram,
                   g_pif[0], g_pif[1], g_pif[2], g_pif[3],
                   g_pif[4], g_pif[5], g_pif[6], g_pif[7]);
            fflush(stdout);
        }
    }
    g_si_done++;
    ctx->r2 = 0;
}
