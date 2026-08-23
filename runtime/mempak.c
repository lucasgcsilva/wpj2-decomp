/* Implementacao de alta performance do Controller Pak (MemPak) de 32 KB para N64.
 *
 * Mantem a imagem de 32 KB em RAM para leituras e escritas instantaneas (0 ms),
 * evitando I/O de disco a cada bloco de 32 bytes. O salvamento no disco ocorre
 * de forma assincrona/em lote quando o buffer esta marcado como dirty.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mempak.h"

static uint8_t g_mempak_data[MEMPAK_SIZE];
static int g_mempak_loaded = 0;
static int g_mempak_dirty = 0;
static DWORD g_last_save_time = 0;
static const char* g_mempak_filename = "sav\\mempak_port1.mpk";

/* Tabela de CRC8 oficial do controlador N64 */
static uint8_t mempak_crc_table[256];
static int g_crc_table_initialized = 0;

static void init_crc_table(void) {
    if (g_crc_table_initialized) return;
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int j = 0; j < 8; j++) {
            if (c & 0x80) {
                c = (c << 1) ^ 0x85;
            } else {
                c <<= 1;
            }
        }
        mempak_crc_table[i] = c;
    }
    g_crc_table_initialized = 1;
}

uint8_t mempak_calc_data_crc(const uint8_t* data, size_t length) {
    init_crc_table();
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc = mempak_crc_table[crc ^ data[i]];
    }
    return crc;
}

static void mempak_format_default(void) {
    memset(g_mempak_data, 0, sizeof(g_mempak_data));

    /* Pagina 0: ID Page (0x0000 - 0x00FF) */
    g_mempak_data[0x00] = 0x80; /* Bank 0 marker */
    g_mempak_data[0x01] = 0x01;
    g_mempak_data[0x02] = 0x00;
    g_mempak_data[0x03] = 0x01;

    /* Serial N64 padrao */
    g_mempak_data[0x04] = 0x00; g_mempak_data[0x05] = 0x00;
    g_mempak_data[0x06] = 0x00; g_mempak_data[0x07] = 0x01;

    /* Checksums de ID */
    uint16_t csum = 0;
    for (int i = 0; i < 28; i += 2) {
        csum += (uint16_t)((g_mempak_data[i] << 8) | g_mempak_data[i + 1]);
    }
    g_mempak_data[0x1C] = (uint8_t)(csum >> 8);
    g_mempak_data[0x1D] = (uint8_t)(csum & 0xFF);
    g_mempak_data[0x1E] = (uint8_t)(~csum >> 8);
    g_mempak_data[0x1F] = (uint8_t)(~csum & 0xFF);

    /* Paginas 1 & 2: Inode Table */
    for (int i = 0; i < 5; i++) {
        g_mempak_data[0x0100 + i * 2 + 1] = 0x01;
    }

    g_mempak_dirty = 1;
    printf("[mempak] Imagem padrao de Controller Pak de 32 KB formatada.\n");
    fflush(stdout);
}

void mempak_flush(void) {
    if (!g_mempak_dirty) return;
    CreateDirectoryA("sav", NULL);
    FILE* f = fopen(g_mempak_filename, "wb");
    if (f) {
        fwrite(g_mempak_data, 1, sizeof(g_mempak_data), f);
        fclose(f);
        g_mempak_dirty = 0;
        g_last_save_time = GetTickCount();
    }
}

void mempak_init(void) {
    if (g_mempak_loaded) return;
    init_crc_table();

    CreateDirectoryA("sav", NULL);
    FILE* f = fopen(g_mempak_filename, "rb");
    if (f) {
        size_t read_bytes = fread(g_mempak_data, 1, sizeof(g_mempak_data), f);
        fclose(f);
        if (read_bytes == sizeof(g_mempak_data)) {
            printf("[mempak] Controller Pak de 32 KB carregado em RAM a partir de %s\n", g_mempak_filename);
            fflush(stdout);
            g_mempak_loaded = 1;
            g_mempak_dirty = 0;
            return;
        }
    }

    mempak_format_default();
    mempak_flush();
    g_mempak_loaded = 1;
}

int mempak_is_present(void) {
    return 1;
}

int mempak_read_block(uint16_t n64_address, uint8_t* out_data32, uint8_t* out_crc) {
    if (!g_mempak_loaded) mempak_init();

    /* Leitura ultra-rapida direta da RAM (0 ms) */
    uint32_t block_offset = (n64_address & 0xFFE0u);
    if (block_offset + 32 > MEMPAK_SIZE) {
        block_offset %= MEMPAK_SIZE;
    }

    memcpy(out_data32, &g_mempak_data[block_offset], 32);
    if (out_crc) {
        *out_crc = mempak_calc_data_crc(out_data32, 32);
    }

    /* Se houver alteracoes pendentes a mais de 500ms, salva em lote no disco */
    if (g_mempak_dirty && (GetTickCount() - g_last_save_time > 500)) {
        mempak_flush();
    }

    return 0;
}

int mempak_write_block(uint16_t n64_address, const uint8_t* in_data32, uint8_t in_crc, uint8_t* out_crc) {
    if (!g_mempak_loaded) mempak_init();

    /* Escrita ultra-rapida direta na RAM (0 ms) */
    uint32_t block_offset = (n64_address & 0xFFE0u);
    if (block_offset + 32 > MEMPAK_SIZE) {
        block_offset %= MEMPAK_SIZE;
    }

    memcpy(&g_mempak_data[block_offset], in_data32, 32);
    uint8_t crc = mempak_calc_data_crc(in_data32, 32);
    if (out_crc) {
        *out_crc = crc;
    }

    g_mempak_dirty = 1;

    /* Salva em lote a cada 500ms de inatividade ou ao finalizar gravacoes */
    if (GetTickCount() - g_last_save_time > 500) {
        mempak_flush();
    }

    return 0;
}
