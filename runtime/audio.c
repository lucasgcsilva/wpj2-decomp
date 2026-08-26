/* Captura e reproducao de audio do AI do N64 no host.
 *
 * N64Recomp guarda meia-palavra em ordem nativa com o mesmo XOR 2 que usa nos
 * acessos LH/SH. O formato WAV/Windows tambem e PCM little-endian, portanto o
 * valor s16 pode ser escrito diretamente depois de lido nessa convencao. Nesta
 * O WAV mantem uma prova reproduzivel do PCM e a saida WinMM permite que o
 * prototipo toque esse mesmo fluxo durante a execucao. */
#include "runtime.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

static FILE* g_wav;
static FILE* g_buffer_manifest;
/* A primeira inicializacao desta ROM pede a taxa NTSC efetiva de 22.047 Hz
 * (confirmada no WAV produzido pelo AI do Project64).  32 kHz era apenas o
 * valor generico do capturador e fazia AI_LEN consumir amostras depressa
 * demais, deixando cada AList com o minimo fixo de 720 frames. */
static uint32_t g_rate = 22047;
static uint64_t g_buffers, g_bytes;
static uint32_t g_peak;
static uint32_t g_buffer_index;
static int g_enabled;
static int g_play_enabled;
static int g_play_fast_forward;
/* Ganho de apresentacao do host, em Q15. Nao participa da RSP nem grava
 * estados na RDRAM: serve para separar saturacao na saida de PCM corrompido
 * durante as sondagens. */
static int32_t g_master_gain_q15 = 32767;
static HWAVEOUT g_wave;
static char g_buffer_dir[MAX_PATH];

static int16_t audio_apply_master_gain(int16_t sample) {
    int32_t v = ((int32_t)sample * g_master_gain_q15) >> 15;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

/* Quanto esperar por um slot livre antes de desistir, em milissegundos. Zero
   volta ao comportamento antigo de descartar na hora, o que serve para medir a
   diferenca lado a lado. */
static int g_play_wait_ms = 12;
static uint64_t g_play_drops;

#define PLAY_SLOTS 8
typedef struct {
    WAVEHDR header;
    uint8_t* pcm;
    int prepared;
} play_slot;
static play_slot g_play[PLAY_SLOTS];
/* A AI real possui FIFO de dois DMAs. A versao inicial acordava a ROM no VI
 * imediatamente seguinte a osAiSetNextBuffer; isso aproxima um buffer de
 * ~34 ms de um evento de 16,67 ms e descola toda a cadencia do jogo. Mantemos
 * somente tamanho/prazo: o PCM ja foi copiado para host no instante da DMA. */
static uint32_t g_ai_primary_bytes, g_ai_secondary_bytes;
static LARGE_INTEGER g_ai_freq, g_ai_due;
static LARGE_INTEGER g_ai_len_started;
static uint32_t g_ai_len_initial;
/* Cadencia discreta observada nesta ROM a 22.047 Hz/30 ALists por segundo.
 * O construtor arredonda a cada 16 frames; estes restantes produzem exatamente
 * 720,736,752,752,736,720 frames, a sequencia do AI do Project64. */
static int g_ai_virtual_cadence;
static int g_ai_vi_cadence;
static uint32_t g_ai_vi_phase = 1;
static uint32_t g_ai_virtual_remaining;
static uint32_t g_ai_virtual_phase;
static uint64_t g_ai_fifo_full_drops;
/* A AI real possui dois slots. O modo temporizado modela BUSY/FIFO_FULL e a
 * duracao do DMA; osAiSetNextBuffer consulta esse estado pelo wrapper nativo
 * de __osAiDeviceBusy, seguindo a implementacao de referencia do libreultra. */
static int g_ai_timed;
static int g_ai_compat_pending;
/* Modo de sondagem: preserva a aceitaçao compatível de osAiSetNextBuffer,
 * mas agenda o OS_EVENT_AI na duraçao real do PCM em vez do próximo VI. */
static int g_ai_compat_clocked;
static LARGE_INTEGER g_ai_compat_due;
static int g_ai_compat_active;
/* Perfil Zelda64Recomp/N64ModernRuntime: o jogo consulta a quantidade de
 * frames ainda enfileirados no dispositivo de áudio. WinMM não oferece uma
 * fila de bytes idêntica à SDL, então mantemos o mesmo contrato com um
 * contador de frames consumido pelo relógio do dispositivo. É opt-in para
 * comparar a cadência sem trocar o sintetizador RSP. */
static int g_ai_zelda_queue;
static uint64_t g_zq_frames;
static LARGE_INTEGER g_zq_last;

static void audio_ai_clock_init(void);

static void audio_zq_update(void) {
    if (!g_ai_zelda_queue) return;
    audio_ai_clock_init();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_zq_last.QuadPart) {
        g_zq_last = now;
        return;
    }
    uint64_t elapsed = now.QuadPart > g_zq_last.QuadPart
        ? (uint64_t)(now.QuadPart - g_zq_last.QuadPart) : 0;
    uint64_t consumed = (elapsed * (uint64_t)g_rate) / (uint64_t)g_ai_freq.QuadPart;
    if (consumed >= g_zq_frames) g_zq_frames = 0;
    else g_zq_frames -= consumed;
    g_zq_last = now;
}

static void audio_ai_clock_init(void) {
    if (!g_ai_freq.QuadPart) QueryPerformanceFrequency(&g_ai_freq);
}

static void audio_ai_arm_primary(uint32_t bytes) {
    LARGE_INTEGER now;
    audio_ai_clock_init();
    QueryPerformanceCounter(&now);
    /* PCM estereo S16: quatro bytes por frame. Arredondar para cima impede
     * sinalizar conclusao antes de o ultimo frame ter sido consumido. */
    uint64_t ticks = ((uint64_t)bytes * (uint64_t)g_ai_freq.QuadPart +
                      (uint64_t)g_rate * 4u - 1u) /
                     ((uint64_t)g_rate * 4u);
    if (!ticks) ticks = 1;
    g_ai_primary_bytes = bytes;
    g_ai_due.QuadPart = now.QuadPart + (LONGLONG)ticks;
}

/* Valor observavel de AI_LEN. O jogo usa o restante do DMA, nao apenas a
 * notificacao de conclusao, para decidir quantos frames sintetizar no proximo
 * AList. Deixar o ultimo tamanho constante forca sempre 720 frames e cria o
 * descompasso que aparece como chiado acumulado. */
uint32_t audio_ai_length(void) {
    if (g_ai_zelda_queue) {
        /* Mesmo ajuste de segurança usado pelo Zelda64Recomp: reporte meio
         * VI a menos que a fila física, para o jogo nunca sintetizar tarde. */
        audio_zq_update();
        uint64_t bytes = g_zq_frames * 4u;
        uint64_t safety = ((uint64_t)g_rate / 120u) * 4u;
        return (uint32_t)(bytes > safety ? bytes - safety : 0u);
    }
    if (g_ai_vi_cadence) {
        /* O construtor de ALists desperta aproximadamente a cada dois VIs.
         * Indexar o ciclo pelo tempo emulado impede que uma tarefa host mais
         * lenta desloque todas as fases seguintes. */
        static const uint16_t remaining[] = { 416, 352, 288, 288, 352, 416 };
        uint64_t audio_tick = hle_retraces() / 2u;
        return remaining[(audio_tick + g_ai_vi_phase) %
                         (sizeof(remaining) / sizeof(remaining[0]))];
    }
    if (g_ai_virtual_cadence) return g_ai_virtual_remaining;
    if (g_ai_timed) {
        if (!g_ai_primary_bytes || !g_ai_freq.QuadPart) return 0;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= g_ai_due.QuadPart) return 0;
        uint64_t ticks = (uint64_t)(g_ai_due.QuadPart - now.QuadPart);
        uint64_t bytes = (ticks * (uint64_t)g_rate * 4u +
                          (uint64_t)g_ai_freq.QuadPart - 1u) /
                         (uint64_t)g_ai_freq.QuadPart;
        if (bytes > g_ai_primary_bytes) bytes = g_ai_primary_bytes;
        return (uint32_t)bytes & ~7u;
    }
    if (!g_ai_len_initial) return 0;
    audio_ai_clock_init();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t elapsed = now.QuadPart > g_ai_len_started.QuadPart
        ? (uint64_t)(now.QuadPart - g_ai_len_started.QuadPart) : 0;
    uint64_t consumed = (elapsed * (uint64_t)g_rate * 4u) / (uint64_t)g_ai_freq.QuadPart;
    if (consumed >= g_ai_len_initial) return 0;
    return (uint32_t)(g_ai_len_initial - consumed) & ~1u;
}

uint32_t audio_ai_status(void) {
    if (!g_ai_timed) return 0;
    uint32_t status = 0;
    if (g_ai_primary_bytes) status |= 0x40000000u; /* AI_STATUS_DMA_BUSY */
    if (g_ai_primary_bytes && g_ai_secondary_bytes)
        status |= 0x80000000u;                    /* AI_STATUS_FIFO_FULL */
    return status;
}

static void put_u16(FILE* f, uint16_t v) {
    fputc(v & 0xFF, f); fputc(v >> 8, f);
}
static void put_u32(FILE* f, uint32_t v) {
    put_u16(f, (uint16_t)v); put_u16(f, (uint16_t)(v >> 16));
}
static void write_header(FILE* f, uint32_t data_bytes) {
    fwrite("RIFF", 1, 4, f); put_u32(f, 36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16); put_u16(f, 1); put_u16(f, 2);
    put_u32(f, g_rate); put_u32(f, g_rate * 4); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, data_bytes);
}

/* Opcional e estritamente diagnostico: grava cada DMA AI ja convertido para
 * PCM little-endian. O Project64-oraculo produz o mesmo formato, permitindo
 * comparar primeiro buffer divergente sem alinhar WAVs longos. */
static void audio_buffer_capture(uint8_t* rdram, uint32_t p, uint32_t bytes) {
    if (!g_buffer_manifest || !g_buffer_dir[0]) return;
    char filename[64], path[MAX_PATH + 80];
    uint32_t index = ++g_buffer_index, peak = 0, nonzero = 0;
    snprintf(filename, sizeof(filename), "ai_%05u.pcm", index);
    snprintf(path, sizeof(path), "%s\\%s", g_buffer_dir, filename);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    for (uint32_t i = 0; i < bytes; i += 2) {
        uint16_t u = *(const uint16_t*)((uintptr_t)(rdram + p + i) ^ 2u);
        int32_t s = (int16_t)u;
        uint32_t mag = (uint32_t)(s < 0 ? -s : s);
        if (mag > peak) peak = mag;
        if (s) nonzero++;
        put_u16(f, u);
    }
    fclose(f);
    fprintf(g_buffer_manifest, "%u,%06X,%u,%u,%u,%u,%s\n", index, p,
            bytes, bytes / 4u, peak, nonzero, filename);
    fflush(g_buffer_manifest);
}

static void audio_play_reap(int force) {
    for (int i = 0; i < PLAY_SLOTS; i++) {
        play_slot* s = &g_play[i];
        if (!s->pcm || (!force && !(s->header.dwFlags & WHDR_DONE))) continue;
        if (s->prepared && g_wave) waveOutUnprepareHeader(g_wave, &s->header, sizeof(s->header));
        free(s->pcm);
        memset(s, 0, sizeof(*s));
    }
}

static void audio_play_open(void) {
    if (!g_play_enabled || g_wave) return;
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = g_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = g_rate * fmt.nBlockAlign;
    if (waveOutOpen(&g_wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_play_enabled = 0;
        printf("[audio] dispositivo WinMM indisponivel; a captura WAV continua\n");
    }
}

static void audio_play_buffer(uint8_t* rdram, uint32_t p, uint32_t bytes) {
    audio_play_open();
    if (!g_wave) return;
    audio_play_reap(0);
    /* Fila cheia: esperar, nao descartar.
     *
     * A versao anterior devolvia sem enfileirar quando os oito slots estavam
     * ocupados. Cada descarte tira um pedaco do meio da forma de onda e emenda
     * o que vem depois - uma descontinuidade de fase por buffer. A 22 kHz isso
     * nao soa como falha isolada, soa como chiado continuo.
     *
     * A comparacao com o Project64 mostrou o padrao: nos primeiros 11 s, com a
     * fila ainda vazia, a correlacao com o oraculo fica em 0,95-0,98; a partir
     * dai ela cai para 0,10-0,17 e o alinhamento passa a saltar em blocos de
     * ~720 amostras, que e o tamanho do buffer. O Project64 bloqueia em vez de
     * descartar (`while cheio: Sleep(1)`), e e o que fazemos aqui.
     *
     * A espera e limitada: travar o jogo para nao perder audio seria trocar um
     * defeito por outro pior. Se o limite estourar, contamos - assim o descarte
     * deixa de ser silencioso. */
    play_slot* slot = NULL;
    for (int tentativa = 0; tentativa < g_play_wait_ms && !slot; tentativa++) {
        for (int i = 0; i < PLAY_SLOTS; i++)
            if (!g_play[i].pcm) { slot = &g_play[i]; break; }
        if (!slot) { Sleep(1); audio_play_reap(0); }
    }
    if (!slot) {
        for (int i = 0; i < PLAY_SLOTS; i++)
            if (!g_play[i].pcm) { slot = &g_play[i]; break; }
    }
    if (!slot) { g_play_drops++; return; }
    slot->pcm = (uint8_t*)malloc(bytes);
    if (!slot->pcm) return;
    for (uint32_t i = 0; i < bytes; i += 2) {
        int16_t sample = *(const int16_t*)((uintptr_t)(rdram + p + i) ^ 2u);
        *(int16_t*)(slot->pcm + i) = audio_apply_master_gain(sample);
    }
    slot->header.lpData = (LPSTR)slot->pcm;
    slot->header.dwBufferLength = bytes;
    if (waveOutPrepareHeader(g_wave, &slot->header, sizeof(slot->header)) != MMSYSERR_NOERROR ||
        waveOutWrite(g_wave, &slot->header, sizeof(slot->header)) != MMSYSERR_NOERROR) {
        if (slot->header.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(g_wave, &slot->header, sizeof(slot->header));
        free(slot->pcm);
        memset(slot, 0, sizeof(*slot));
        return;
    }
    slot->prepared = 1;
}

void audio_set_fast_forward(int enabled) {
    enabled = enabled != 0;
    if (enabled == g_play_fast_forward) return;
    g_play_fast_forward = enabled;
    /* Audio em tempo real nao pode pautar uma simulacao acelerada. Ao entrar no
       avanco, descarte a fila hospedada; ao soltar, o proximo DMA volta a ser
       ouvido ja no ponto corrente, sem segundos de audio antigo acumulado. */
    if (enabled && g_wave) {
        waveOutReset(g_wave);
        audio_play_reap(1);
    }
}

void audio_init(void) {
    const char* espera = getenv("WPJ2_AUDIO_WAIT_MS");
    if (espera) {
        int v = atoi(espera);
        if (v >= 0 && v <= 200) g_play_wait_ms = v;
    }
    const char* gain_percent = getenv("WPJ2_AUDIO_GAIN_PERCENT");
    if (gain_percent && *gain_percent) {
        int percent = atoi(gain_percent);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        g_master_gain_q15 = (percent * 32767) / 100;
    }
    const char* timed = getenv("WPJ2_AI_TIMED");
    g_ai_timed = timed && *timed && *timed != '0';
    const char* compat_clocked = getenv("WPJ2_AI_COMPAT_CLOCKED");
    g_ai_compat_clocked = compat_clocked && *compat_clocked && *compat_clocked != '0';
    g_ai_compat_pending = 0;
    g_ai_compat_active = 0;
    g_ai_compat_due.QuadPart = 0;
    g_ai_primary_bytes = g_ai_secondary_bytes = 0;
    g_ai_len_initial = 0;
    g_ai_len_started.QuadPart = 0;
    g_ai_virtual_remaining = 0;
    g_ai_virtual_phase = 0;
    g_zq_frames = 0;
    g_zq_last.QuadPart = 0;
    {
        const char* zelda_queue = getenv("WPJ2_AI_ZELDA_QUEUE");
        g_ai_zelda_queue = zelda_queue && *zelda_queue && *zelda_queue != '0';
    }
    {
        const char* virtual_cadence = getenv("WPJ2_AI_VIRTUAL_CADENCE");
        g_ai_virtual_cadence = virtual_cadence && *virtual_cadence &&
                               *virtual_cadence != '0';
    }
    {
        const char* vi_cadence = getenv("WPJ2_AI_VI_CADENCE");
        g_ai_vi_cadence = vi_cadence && *vi_cadence && *vi_cadence != '0';
        const char* vi_phase = getenv("WPJ2_AI_VI_PHASE");
        if (vi_phase && *vi_phase) g_ai_vi_phase = (uint32_t)atoi(vi_phase) % 6u;
    }
    g_ai_due.QuadPart = 0;
    g_buffer_index = 0;
    g_buffer_dir[0] = 0;
    const char* capture_dir = getenv("WPJ2_AUDIO_BUFFER_DIR");
    if (capture_dir && *capture_dir) {
        strncpy(g_buffer_dir, capture_dir, sizeof(g_buffer_dir) - 1);
        g_buffer_dir[sizeof(g_buffer_dir) - 1] = 0;
        CreateDirectoryA(g_buffer_dir, NULL);
        char manifest[MAX_PATH + 80];
        snprintf(manifest, sizeof(manifest), "%s\\wpj2_ai_manifest.csv", g_buffer_dir);
        g_buffer_manifest = fopen(manifest, "wb");
        if (g_buffer_manifest) {
            fprintf(g_buffer_manifest, "indice,endereco,tamanho,frames,pico,nao_zero,arquivo\n");
            fflush(g_buffer_manifest);
        }
    }
    const char* play = getenv("WPJ2_AUDIO_PLAY");
    g_play_enabled = play && *play && *play != '0';
    const char* enable = getenv("WPJ2_AUDIO");
    if (!enable || !*enable || *enable == '0') return;
    const char* path = getenv("WPJ2_AUDIO_WAV");
    if (!path || !*path) path = "temp\\projeto\\audio_capture.wav";
    g_wav = fopen(path, "wb");
    if (!g_wav) {
        printf("[audio] nao foi possivel criar %s\n", path);
        return;
    }
    write_header(g_wav, 0);
    g_enabled = 1;
    printf("[audio] captura PCM habilitada: %s\n", path);
    fflush(stdout);
}

void audio_shutdown(void) {
    if (g_wave) {
        waveOutReset(g_wave);
        audio_play_reap(1);
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    if (g_buffer_manifest) {
        fclose(g_buffer_manifest);
        g_buffer_manifest = NULL;
    }
    rsp_audio_probe_flush();
    if (!g_wav) return;
    if (g_bytes > UINT32_MAX) g_bytes = UINT32_MAX;
    fseek(g_wav, 0, SEEK_SET);
    write_header(g_wav, (uint32_t)g_bytes);
    fclose(g_wav);
    g_wav = NULL;
    printf("[audio] %llu buffer(s), %llu bytes PCM, pico=%u, fifo_descartados=%llu\n",
           (unsigned long long)g_buffers, (unsigned long long)g_bytes, g_peak,
           (unsigned long long)g_ai_fifo_full_drops);
    /* Descarte na saida do host e a causa que a comparacao com o oraculo
       apontou; deixa-lo visivel evita que ele volte a passar despercebido. */
    printf("[audio] fila do host: %llu buffer(s) perdido(s) apos esperar %d ms\n",
           (unsigned long long)g_play_drops, g_play_wait_ms);
}

void audio_set_frequency(uint32_t hz) {
    if (hz >= 8000 && hz <= 96000) g_rate = hz;
}

void audio_queue_ai_buffer(uint8_t* rdram, uint32_t address, uint32_t bytes) {
    uint32_t p = address & 0x1FFFFFFFu;
    /* O AI real entrega sempre multiplos de 8 bytes: o proprio Project64 faz
       `& ~0x7` ao reportar o que resta. Truncar em 2 bytes deixa meia amostra
       estereo sobrando e desloca a fase de todo o buffer seguinte. */
    bytes &= ~7u;
    audio_ai_clock_init();
    if (!g_ai_timed) {
        QueryPerformanceCounter(&g_ai_len_started);
        g_ai_len_initial = bytes;
    }
    if (g_ai_virtual_cadence && !g_ai_vi_cadence) {
        static const uint16_t remaining[] = { 416, 352, 288, 288, 352, 416 };
        g_ai_virtual_remaining = remaining[g_ai_virtual_phase++ %
                                           (sizeof(remaining) / sizeof(remaining[0]))];
    }
    if (!p || p >= 0x800000u || bytes == 0) return;
    if (bytes > 0x800000u - p) bytes = 0x800000u - p;
    bytes &= ~1u;
    if (g_ai_zelda_queue) {
        audio_zq_update();
        g_zq_frames += bytes / 4u;
    }
    if (g_ai_timed) {
        /* Equivalente ao AI_LEN: aceita um DMA primario e, enquanto ele toca,
         * um segundo DMA. */
        if (!g_ai_primary_bytes) audio_ai_arm_primary(bytes);
        else if (!g_ai_secondary_bytes) g_ai_secondary_bytes = bytes;
        else {
            g_ai_fifo_full_drops++;
            return;
        }
    } else {
        /* Compatibilidade: o proximo VI entrega OS_EVENT_AI. Isto mantem a
         * thread de audio alimentando o host enquanto AI_STATUS ainda nao e
         * modelado como MMIO. A cadencia de retrace continua fixa em 60 Hz. */
        if (g_ai_compat_clocked) {
            LARGE_INTEGER now;
            audio_ai_clock_init();
            QueryPerformanceCounter(&now);
            uint64_t ticks = ((uint64_t)bytes * (uint64_t)g_ai_freq.QuadPart +
                              (uint64_t)g_rate * 4u - 1u) /
                             ((uint64_t)g_rate * 4u);
            g_ai_compat_due.QuadPart = now.QuadPart + (LONGLONG)(ticks ? ticks : 1);
            g_ai_compat_active = 1;
            g_ai_compat_pending = 0;
        } else {
            g_ai_compat_pending = 1;
        }
    }
    /* Sonda passiva: compara o PCM que saiu do RSP com os mesmos bytes no
       instante em que osAiSetNextBuffer os entrega ao dispositivo AI. */
    rsp_audio_probe_ai_buffer(rdram, p, bytes);
    /* A interrupcao AI faz parte da emulacao mesmo quando a captura WAV esta
       desligada. O arquivo e apenas uma observacao opcional. */
    if (g_play_enabled && !g_play_fast_forward)
        audio_play_buffer(rdram, p, bytes);
    audio_buffer_capture(rdram, p, bytes);
    if (!g_enabled || !g_wav) return;
    /* Limite de 16 MiB para uma sonda curta nao consumir o disco sem controle. */
    if (g_bytes >= 16u * 1024u * 1024u) return;
    if (bytes > 16u * 1024u * 1024u - g_bytes) bytes = (uint32_t)(16u * 1024u * 1024u - g_bytes);
    bytes &= ~1u;
    for (uint32_t i = 0; i < bytes; i += 2) {
        int16_t output = audio_apply_master_gain(
            *(const int16_t*)((uintptr_t)(rdram + p + i) ^ 2u));
        uint16_t u = (uint16_t)output;
        int32_t s = output;
        uint32_t mag = (uint32_t)(s < 0 ? -s : s);
        if (mag > g_peak) g_peak = mag;
        put_u16(g_wav, u);
    }
    g_buffers++;
    g_bytes += bytes;
}

uint64_t audio_buffers_queued(void) { return g_buffers; }
uint64_t audio_bytes_queued(void) { return g_bytes; }
uint32_t audio_peak_sample(void) { return g_peak; }
int audio_ai_done_pending(void) {
    if (!g_ai_timed) {
        if (!g_ai_compat_clocked) return g_ai_compat_pending;
        if (!g_ai_compat_active || !g_ai_freq.QuadPart) return 0;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart >= g_ai_compat_due.QuadPart;
    }
    if (!g_ai_primary_bytes || !g_ai_freq.QuadPart) return 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart >= g_ai_due.QuadPart;
}

void audio_take_ai_done(void) {
    if (!g_ai_timed) {
        g_ai_compat_pending = 0;
        g_ai_compat_active = 0;
        g_ai_compat_due.QuadPart = 0;
        return;
    }
    if (!audio_ai_done_pending()) return;
    if (g_ai_secondary_bytes) {
        uint32_t next = g_ai_secondary_bytes;
        g_ai_secondary_bytes = 0;
        audio_ai_arm_primary(next);
    } else {
        g_ai_primary_bytes = 0;
        g_ai_due.QuadPart = 0;
    }
}
