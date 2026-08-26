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
#include "mempak.h"

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
/* Definicao tentativa: o contador vive mais abaixo, junto do roteiro por
   leitura, mas a gravacao de reproducao precisa dele aqui em cima. */
static uint64_t g_polls_controle;
static int8_t g_stick_x = 0, g_stick_y = 0;

static void cancelar_roteiro_por_entrada_ao_vivo(void) {
    if (g_roteiro_n || g_roteiro_poll_n) {
        printf("entr : entrada ao vivo; roteiro gravado cancelado\n");
        fflush(stdout);
        g_roteiro_n = 0;
        g_roteiro_poll_n = 0;
    }
}

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
    cancelar_roteiro_por_entrada_ao_vivo();
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

/* Gravacao de entrada para reproducao posterior.
 *
 * Por que isto existe, e por que nao um savestate. Um savestate ao estilo
 * Project64 nao e alcancavel aqui: o PJ64 emula a CPU e guarda todo o estado
 * numa struct, enquanto neste projeto cada thread do N64 e uma fiber do
 * Windows e a posicao de execucao dela E a pilha de chamadas C do codigo
 * recompilado. O Win32 nao expoe o contexto salvo de uma fiber, e a pilha
 * carrega ponteiros do host. Restaurar so a RDRAM - que e o que o F2/F4 atual
 * faz - troca o mundo por baixo de fibers que continuam achando que estao
 * noutro ponto; daí travar com facilidade.
 *
 * O que se quer de verdade e chegar rapido e de forma repetivel a uma cena
 * para analisar. Isso a reproducao resolve por construcao, sem nenhum risco de
 * inconsistencia: e uma execucao normal, so que com a entrada vindo de um
 * roteiro em vez do teclado.
 *
 * O indice e a CONTAGEM DE LEITURAS do controle, nao o relogio. Milissegundos
 * escorregam conforme a carga do host - foi o motivo de g_roteiro_poll existir
 * - enquanto a n-esima leitura e sempre a n-esima leitura. */
#define GRAVACAO_MAX 4096
static struct {
    uint64_t poll;
    uint16_t botoes;
    int8_t stick_x, stick_y;
} g_gravacao[GRAVACAO_MAX];
static unsigned g_gravacao_n = 0;

void pif_gravar_transicao(uint16_t b) {
    static uint16_t ultimo = 0;
    static int8_t ultimo_x = 0, ultimo_y = 0;
    static int primeira = 1;
    if (!primeira && b == ultimo && g_stick_x == ultimo_x &&
        g_stick_y == ultimo_y) return;
    primeira = 0;
    ultimo = b;
    ultimo_x = g_stick_x;
    ultimo_y = g_stick_y;
    if (g_gravacao_n < GRAVACAO_MAX) {
        g_gravacao[g_gravacao_n].poll = g_polls_controle;
        g_gravacao[g_gravacao_n].botoes = b;
        g_gravacao[g_gravacao_n].stick_x = g_stick_x;
        g_gravacao[g_gravacao_n].stick_y = g_stick_y;
        g_gravacao_n++;
    }
}

int pif_gravar_replay(const char* caminho) {
    FILE* f = fopen(caminho, "w");
    if (!f) return 0;
    /* Formato deliberadamente igual ao aceito por WPJ2_INPUT_POLLS, para poder
       colar direto numa linha de comando sem conversao. */
    fprintf(f, "# reproducao wpj2: alvo=%llu leituras\n",
            (unsigned long long)g_polls_controle);
    fprintf(f, "alvo=%llu\n", (unsigned long long)g_polls_controle);
    fprintf(f, "roteiro=");
    for (unsigned i = 0; i < g_gravacao_n; i++)
        fprintf(f, "%s%llu:%04X@%d,%d", i ? ";" : "",
                (unsigned long long)g_gravacao[i].poll,
                g_gravacao[i].botoes, (int)g_gravacao[i].stick_x,
                (int)g_gravacao[i].stick_y);
    fprintf(f, "\n");
    fclose(f);
    printf("[replay] gravado %s: %u transicao(oes), alvo %llu leituras\n",
           caminho, g_gravacao_n, (unsigned long long)g_polls_controle);
    fflush(stdout);
    return 1;
}

uint64_t pif_polls_atuais(void) { return g_polls_controle; }

/* WPJ2_STICK aceita "x,y" (por exemplo "80,0"). O analogico antes ficava
 * sempre centrado, deixando sem teste uma entrada que o jogo pode usar para
 * navegar depois da tela inicial. */
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
#define ROTEIRO_MAX 4096
static struct { uint32_t ms; uint16_t botoes; } g_roteiro[ROTEIRO_MAX];
static int g_roteiro_n = 0;
static DWORD g_t0 = 0;

/* Roteiro deterministico, contado nas leituras CMD_READ_BTN que o proprio jogo
 * faz. O roteiro em milissegundos e util para uma pessoa, mas sob dezenas de
 * processos em paralelo o escalonador do host desloca uma janela de 50 ms. */
static struct {
    uint32_t poll;
    uint16_t botoes;
    int8_t stick_x, stick_y;
    uint8_t tem_stick;
} g_roteiro_poll[ROTEIRO_MAX];
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
        g_roteiro_poll[g_roteiro_poll_n].stick_x = 0;
        g_roteiro_poll[g_roteiro_poll_n].stick_y = 0;
        g_roteiro_poll[g_roteiro_poll_n].tem_stick = 0;
        if (fim && *fim == '@') {
            long x = strtol(fim + 1, &fim, 10);
            long y = 0;
            if (fim && *fim == ',') y = strtol(fim + 1, &fim, 10);
            if (x < -128) x = -128;
            if (x > 127) x = 127;
            if (y < -128) y = -128;
            if (y > 127) y = 127;
            g_roteiro_poll[g_roteiro_poll_n].stick_x = (int8_t)x;
            g_roteiro_poll[g_roteiro_poll_n].stick_y = (int8_t)y;
            g_roteiro_poll[g_roteiro_poll_n].tem_stick = 1;
        }
        g_roteiro_poll_n++;
        if (!fim || *fim != ';') break;
        s = fim + 1;
    }
    g_t0 = GetTickCount();
    printf("entr : roteiro por leitura com %d passo(s)\n", g_roteiro_poll_n);
}

/* O estado corrente e o ultimo passo cujo instante ja passou. */
static uint16_t botoes_agora(void) {
    if (g_roteiro_poll_n) {
        uint16_t b = 0;
        for (int i = 0; i < g_roteiro_poll_n; i++) {
            if (g_roteiro_poll[i].poll > g_polls_controle) continue;
            b = g_roteiro_poll[i].botoes;
            if (g_roteiro_poll[i].tem_stick) {
                g_stick_x = g_roteiro_poll[i].stick_x;
                g_stick_y = g_roteiro_poll[i].stick_y;
            }
        }
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

void pif_update_stick_from_keys(int up, int down, int left, int right) {
    cancelar_roteiro_por_entrada_ao_vivo();
    int8_t sx = 0, sy = 0;
    if (up)    sy += 80;
    if (down)  sy -= 80;
    if (right) sx += 80;
    if (left)  sx -= 80;
    g_stick_x = sx;
    g_stick_y = sy;
}

/* Percorre a fita de comandos e preenche as respostas no lugar.
 *
 * `refresh` distingue as duas origens, e a distincao importa:
 *
 *   refresh=0  execucao da fita, disparada por SI_PIF_ADDR_WR64B (dir=1).
 *              O jogo acabou de montar comandos novos; todos valem.
 *   refresh=1  releitura, disparada por SI_PIF_ADDR_RD64B (dir=0). Só o
 *              estado dos controles e reamostrado.
 *
 * Por que reamostrar na leitura. A libultra escreve a fita uma unica vez e
 * depois so le de volta (tools/libreultra/src/io/contreaddata.c); no hardware
 * os botoes chegam novos porque o PIF varre os controles CONTINUAMENTE para a
 * sua RAM, em segundo plano - nao porque a fita seja reexecutada. Reprocessar
 * os comandos de controle no dir=0 e a forma mais simples de reproduzir essa
 * varredura. Sem isso o controle congela: medido em 23/08, o jogo lia 30 vezes
 * por segundo uma resposta de dois segundos atras.
 *
 * Por que armazenamento fica de fora. A varredura de fundo do PIF cobre botoes
 * e analogico, jamais o Controller Pack: ler ou gravar 32 bytes so acontece
 * quando o jogo pede. Tratar 0x02/0x03 no refresh executa cada transacao duas
 * vezes - inofensivo no conteudo, caro no tempo. */
static void pif_process(int refresh) {
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
                out[2] = mempak_is_present() ? 0x01 : 0x00; /* 0x01 = CONT_CARD_ON (MemPak inserido!) */
            }
        } else if (cmd == CMD_READ_BTN) {
            if (rx >= 4) {
                g_polls_controle++;
                uint16_t b = botoes_agora();
                /* Gravar AQUI, e nao em pif_set_buttons.
                 *
                 * Este e o unico ponto por onde passa toda entrada, venha do
                 * teclado ou de um roteiro, e o indice de leitura ja esta
                 * correto. Gravando na tecla, uma sessao que comecasse de uma
                 * reproducao registraria so o que fosse digitado por cima e
                 * perderia o roteiro que a trouxe ate ali. */
                pif_gravar_transicao(b);
                /* Registrar por MUDANCA, nao pelas primeiras N leituras. O
                 * limite antigo se esgotava no boot e nada aparecia quando a
                 * pessoa apertava uma tecla no titulo, minutos depois - que e
                 * justamente o instante que interessa. */
                static uint16_t ultimo = 0;
                static int8_t ultimo_x = 0, ultimo_y = 0;
                static int primeira = 1;
                if (b != ultimo || g_stick_x != ultimo_x ||
                    g_stick_y != ultimo_y || primeira) {
                    primeira = 0;
                    printf("[pif] MUDOU botoes=%04X stick=%d,%d na leitura %llu\n",
                           b, (int)g_stick_x, (int)g_stick_y,
                           (unsigned long long)g_polls_controle);
                    fflush(stdout);
                    ultimo = b;
                    ultimo_x = g_stick_x;
                    ultimo_y = g_stick_y;
                }
                if (g_polls_controle_logados++ < 32) {
                    printf("[pif] leitura=%llu tempo=%lu ms botoes=%04X stick=%d,%d\n",
                           (unsigned long long)g_polls_controle,
                           (unsigned long)(GetTickCount() - g_t0), b,
                           (int)g_stick_x, (int)g_stick_y);
                    fflush(stdout);
                }
                out[0] = (uint8_t)(b >> 8);
                out[1] = (uint8_t)(b & 0xFF);
                out[2] = (uint8_t)g_stick_x;
                out[3] = (uint8_t)g_stick_y;
            }
        } else if (cmd == 0x02) { /* READ_PAK: Leitura de 32 bytes do MemPak */
            /* Armazenamento nao entra no refresh - ver comentario de
             * pif_process(). Reexecutar aqui so repetiria a mesma leitura. */
            if (refresh) { i += 2 + tx + rx; channel++; continue; }
            if (tx >= 3 && rx >= 33) {
                uint16_t addr = (uint16_t)((g_pif[i + 3] << 8) | g_pif[i + 4]);
                uint8_t crc = 0;
                mempak_read_block(addr, out, &crc);
                out[32] = crc;
            }
        } else if (cmd == 0x03) { /* WRITE_PAK: Escrita de 32 bytes no MemPak */
            /* CRITICO: sem esta guarda a escrita acontece DUAS vezes por
             * transacao, uma na execucao da fita e outra no refresh. O
             * conteudo continua correto (mesmos 32 bytes no mesmo endereco),
             * entao o defeito nao aparece como save corrompido - aparece como
             * lentidao, que foi exatamente o sintoma relatado. */
            if (refresh) { i += 2 + tx + rx; channel++; continue; }
            if (tx >= 35 && rx >= 1) {
                uint16_t addr = (uint16_t)((g_pif[i + 3] << 8) | g_pif[i + 4]);
                uint8_t in_crc = g_pif[i + 5 + 32];
                uint8_t out_crc = 0;
                mempak_write_block(addr, &g_pif[i + 5], in_crc, &out_crc);
                out[0] = out_crc;
            }
        } else {
            /* Outros acessorios nao suportados */
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
        pif_process(0);
        g_si_writes++;
    } else {
        /* PIF -> RDRAM: devolve a fita ja com as respostas.
         *
         * O refresh antes da copia reamostra o estado dos controles, imitando
         * a varredura de fundo do PIF. O porque, e por que armazenamento fica
         * de fora, estao no comentario de pif_process(). Sem ele o controle
         * congela: si_w parava em 29 aos 2,0 s enquanto si_r seguia a ~30/s,
         * ou seja o jogo lia trinta vezes por segundo uma resposta de dois
         * segundos atras. */
        pif_process(1);
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
