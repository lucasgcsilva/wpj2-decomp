/* Legendas PT-BR experimentais para o formatador interno de Wonder Project J2.
 *
 * A ROM avanca o ponteiro da cadeia a cada chamada para fazer a digitacao
 * progressiva. Nesta primeira versao, cada traducao ocupa exatamente o mesmo
 * numero de bytes da cadeia inglesa: texto maior e cortado, texto menor recebe
 * espacos. Assim o cursor que a ROM grava de volta continua valido. A camada
 * so existe quando WPJ2_LEGENDAS aponta para o TSV, ou vale 1. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "legendas.h"

#define RDRAM_LIMIT          0x00400000u /* o jogo foi inicializado como 4 MB */
#define SCRATCH_BASE         0x007F0000u /* metade alta reservada ao host */
#define SCRATCH_SLOT_BYTES   1024u
#define MAX_ENTRIES          4096u
#define MAX_TEXT             511u
#define MAX_ACTIVE           16u
#define HASH_BUCKETS         8191u

typedef struct {
    char* source;
    char* translated;
    uint16_t source_len;
    int next;
} subtitle_entry_t;

typedef struct {
    uint32_t args_phys;
    uint32_t source_base;
    uint32_t scratch_phys;
    uint16_t length;
    int used;
} active_subtitle_t;

static subtitle_entry_t g_entries[MAX_ENTRIES];
static size_t g_entry_count;
static int g_prefix_buckets[HASH_BUCKETS];
static active_subtitle_t g_active[MAX_ACTIVE];
static int g_initialized;
static int g_enabled;
static int g_scratch_committed;
static FILE* g_trace;
static uint32_t g_trace_lines;

typedef struct {
    active_subtitle_t* active;
    uint32_t args_phys;
    int bound;
} binding_t;
static binding_t g_binding;

static uint32_t rd32(uint8_t* rdram, uint32_t phys) {
    return *(uint32_t*)(rdram + phys);
}

static void wr32(uint8_t* rdram, uint32_t phys, uint32_t value) {
    *(uint32_t*)(rdram + phys) = value;
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

static void make_fixed_ascii(char* out, size_t bytes, const char* text) {
    size_t in = 0, written = 0;
    while (text[in] && written < bytes) {
        size_t advance;
        out[written++] = fold_utf8((const unsigned char*)text + in, &advance);
        in += advance;
    }
    while (written < bytes) out[written++] = ' ';
    out[bytes] = '\0';
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
    if (!fgets(line, sizeof(line), file) || strcmp(line, "source_en\tpt_br\n")) {
        fclose(file);
        return;
    }
    while (g_entry_count < MAX_ENTRIES && fgets(line, sizeof(line), file)) {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = '\0';
        char* end = strpbrk(tab, "\r\n");
        if (end) *end = '\0';
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

static void trace_source(const char* status, const char* source) {
    if (!g_trace || g_trace_lines++ >= 2048u) return;
    fprintf(g_trace, "%s\t%s\n", status, source);
    fflush(g_trace);
}

static int read_source(uint8_t* rdram, uint32_t pointer, char* out) {
    uint32_t phys = pointer & 0x1FFFFFFFu;
    if (phys >= RDRAM_LIMIT) return 0;
    for (uint32_t i = 0; i <= MAX_TEXT && phys + i < RDRAM_LIMIT; i++) {
        uint8_t value = rd8(rdram, phys + i);
        out[i] = (char)value;
        if (!value) return 1;
        if (value < 0x20u || value > 0x7Eu) return 0;
    }
    return 0;
}

static subtitle_entry_t* find_entry(const char* source) {
    for (size_t i = 0; i < g_entry_count; i++)
        if (!strcmp(g_entries[i].source, source)) return &g_entries[i];
    return NULL;
}

static active_subtitle_t* allocate_active(uint32_t args_phys, uint32_t source_base,
                                          subtitle_entry_t* entry, uint8_t* rdram) {
    active_subtitle_t* slot = NULL;
    for (size_t i = 0; i < MAX_ACTIVE; i++)
        if (g_active[i].used && g_active[i].args_phys == args_phys) { slot = &g_active[i]; break; }
    if (!slot)
        for (size_t i = 0; i < MAX_ACTIVE; i++)
            if (!g_active[i].used) { slot = &g_active[i]; break; }
    if (!slot) slot = &g_active[args_phys % MAX_ACTIVE];
    if (!g_scratch_committed) {
        if (!VirtualAlloc(rdram + SCRATCH_BASE, MAX_ACTIVE * SCRATCH_SLOT_BYTES,
                          MEM_COMMIT, PAGE_READWRITE))
            return NULL;
        g_scratch_committed = 1;
    }
    size_t index = (size_t)(slot - g_active);
    slot->args_phys = args_phys;
    slot->source_base = source_base;
    slot->scratch_phys = SCRATCH_BASE + (uint32_t)(index * SCRATCH_SLOT_BYTES);
    slot->length = entry->source_len;
    slot->used = 1;
    char fixed[MAX_TEXT + 1];
    make_fixed_ascii(fixed, entry->source_len, entry->translated);
    for (uint32_t i = 0; i <= entry->source_len; i++)
        wr8(rdram, slot->scratch_phys + i, (uint8_t)fixed[i]);
    return slot;
}

void legendas_antes(uint8_t* rdram, uint32_t args) {
    g_binding.bound = 0;
    init_once();
    uint32_t args_phys = args & 0x1FFFFFFFu;
    if (!g_enabled || args_phys + 4u > RDRAM_LIMIT) return;
    uint32_t original = rd32(rdram, args_phys);
    active_subtitle_t* active = NULL;
    for (size_t i = 0; i < MAX_ACTIVE; i++) {
        active_subtitle_t* candidate = &g_active[i];
        uint32_t delta = original - candidate->source_base;
        if (candidate->used && candidate->args_phys == args_phys && delta <= candidate->length) {
            active = candidate;
            break;
        }
    }
    if (!active) {
        char source[MAX_TEXT + 1];
        if (!read_source(rdram, original, source)) return;
        subtitle_entry_t* entry = find_entry(source);
        if (!entry) {
            trace_source("sem_traducao", source);
            return;
        }
        trace_source("traduzido", source);
        active = allocate_active(args_phys, original, entry, rdram);
        if (!active) return;
    }
    uint32_t delta = original - active->source_base;
    if (delta > active->length) return;
    wr32(rdram, args_phys, 0x80000000u | (active->scratch_phys + delta));
    g_binding.active = active;
    g_binding.args_phys = args_phys;
    g_binding.bound = 1;
}

void legendas_depois(uint8_t* rdram, uint32_t args) {
    (void)args;
    if (!g_binding.bound) return;
    active_subtitle_t* active = g_binding.active;
    uint32_t cursor = rd32(rdram, g_binding.args_phys) & 0x1FFFFFFFu;
    if (cursor >= active->scratch_phys && cursor <= active->scratch_phys + active->length) {
        uint32_t delta = cursor - active->scratch_phys;
        wr32(rdram, g_binding.args_phys, active->source_base + delta);
    }
    g_binding.bound = 0;
}

void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size) {
    init_once();
    if (!g_enabled) return;
    unsigned patched = 0;
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
            make_fixed_ascii(fixed, length, entry->translated);
            for (i = 0; i < length; i++) cart[(pos + i) ^ 3u] = (uint8_t)fixed[i];
            patched++;
        }
    }
    if (g_trace && g_trace_lines++ < 2048u) {
        fprintf(g_trace, "cartucho_aplicado\t%u cadeias\n", patched);
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
}
