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
            case 0xA0: case 0xA1: case 0xA2: case 0xA3: return 'a';
            case 0xA7: return 'c';
            case 0xA9: case 0xAA: return 'e';
            case 0xAD: return 'i';
            case 0xB3: case 0xB4: case 0xB5: return 'o';
            case 0xBA: return 'u';
            default: return '?';
        }
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

static size_t make_fixed_ascii(char* out, size_t bytes, const char* text) {
    size_t written = make_ascii(out, bytes + 1u, text);
    while (written < bytes) out[written++] = ' ';
    out[bytes] = '\0';
    return written;
}

static uint32_t prefix_key_text(const char* text) {
    uint32_t key = 0;
    for (uint32_t i = 0; i < 4u; i++) key |= (uint32_t)(uint8_t)text[i] << (i * 8u);
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
    if (source_n < 4u) return 0;

    subtitle_entry_t* entry = find_entry(source);
    if (!entry) return 0;

    char translated[SCRATCH_SLOT_BYTES];
    size_t translated_n = make_ascii(translated, sizeof(translated), entry->translated);
    size_t encoded_n = translated_n + control_n * 2u;
    if (!translated_n || encoded_n > raw_n) {
        trace_source("recurso_ptbr_longo", source);
        return 0;
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
    while (out <= raw_n)
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
    if (strlen(source) < 4u) return NULL;
    uint32_t bucket = prefix_key_text(source) % HASH_BUCKETS;
    for (int index = g_prefix_buckets[bucket]; index >= 0;
         index = g_entries[index].next)
        if (!strcmp(g_entries[index].source, source)) return &g_entries[index];
    return NULL;
}

void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size) {
    init_once();
    if (!g_enabled) return;
    unsigned patched = 0, skipped_long = 0;
    for (size_t pos = 0; pos + 4u <= rom_size; pos++) {
        uint32_t bucket = prefix_key_cart(cart, pos) % HASH_BUCKETS;
        for (int index = g_prefix_buckets[bucket]; index >= 0; index = g_entries[index].next) {
            subtitle_entry_t* entry = &g_entries[index];
            size_t length = entry->source_len;
            if (pos + length > rom_size) continue;
            size_t i = 0;
            while (i < length && cart[(pos + i) ^ 3u] == (uint8_t)entry->source[i]) i++;
            if (i != length) continue;
            char fixed[MAX_TEXT + 1];
            char translated[MAX_TEXT + 1];
            size_t translated_len = make_ascii(translated, sizeof(translated), entry->translated);
            /* Uma cadeia maior sobrescreveria o recurso seguinte. Ela permanece
             * inglesa no cartucho e sera expandida no interceptador dinamico. */
            if (translated_len > length) {
                skipped_long++;
                continue;
            }
            make_fixed_ascii(fixed, length, entry->translated);
            for (i = 0; i < length; i++) cart[(pos + i) ^ 3u] = (uint8_t)fixed[i];
            patched++;
        }
    }
    if (g_trace && g_trace_lines++ < 2048u) {
        fprintf(g_trace, "cartucho_aplicado\t%u cadeias; %u maiores preservadas\n",
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
