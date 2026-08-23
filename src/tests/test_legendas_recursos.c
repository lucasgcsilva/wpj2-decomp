#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "legendas.h"

#define RDRAM_BYTES 0x00800000u

static void write_n64(uint8_t* rdram, uint32_t phys, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++) rdram[(phys + (uint32_t)i) ^ 3u] = data[i];
}

static int check_n64(uint8_t* rdram, uint32_t phys, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (rdram[(phys + (uint32_t)i) ^ 3u] != data[i]) return 0;
    return 1;
}

int main(void) {
    static const uint8_t source_title[] = "The Siliconian Empire\n";
    static const uint8_t expected_title[] = "O Imperio Siliconiano";
    static const uint8_t source_bird[] = {
        0xE2, 0x02, 'B','i','r','d', 0xE2, 0x02,
        ':',' ','T','h','e',' ','d','o','c','t','o','r',' ','w','a','n','t','s',' ',
        't','o',' ','s','e','e',' ','y','o','u','!','\n',0
    };
    static const uint8_t expected_bird[] = {
        0xE2, 0x02, 'B','i','r','d', 0xE2, 0x02,
        ':',' ','O',' ','d','o','u','t','o','r',' ','q','u','e','r',' ','v','e','r',' ',
        'v','o','c','e','!',0
    };
    static const uint8_t source_geppetto[] = {
        0xE2,0x02,'D','r','.', ' ','G','e','p','p','e','t','t','o',0xE2,0x02,':',' ',
        0xE0,0x01,'.','.','.',' ','l','i','s','t','e','n',' ','v','e','r','y','\n',
        'c','a','r','e','f','u','l','l','y','.','.','.','\n',0
    };
    static const uint8_t expected_geppetto[] = {
        0xE2,0x02,'D','r','.', ' ','G','e','p','p','e','t','t','o',0xE2,0x02,':',' ',
        0xE0,0x01,'.','.','.',' ','o','u','c','a',' ','c','o','m','\n',
        'a','t','e','n','c','a','o','.','.','.',0
    };
    uint8_t* rdram = (uint8_t*)calloc(1, RDRAM_BYTES);
    if (!rdram) return 2;
    _putenv_s("WPJ2_LEGENDAS", "textos\\traducao_ptbr.tsv");

    write_n64(rdram, 0x1000, source_title, sizeof(source_title));
    write_n64(rdram, 0x2000, source_bird, sizeof(source_bird));
    write_n64(rdram, 0x3000, source_geppetto, sizeof(source_geppetto));
    int ok = legendas_substituir_recurso(rdram, 0x80001000u) &&
             legendas_substituir_recurso(rdram, 0x80002000u) &&
             legendas_substituir_recurso(rdram, 0x80003000u) &&
             check_n64(rdram, 0x1000, expected_title, sizeof(expected_title)) &&
             check_n64(rdram, 0x2000, expected_bird, sizeof(expected_bird)) &&
             check_n64(rdram, 0x3000, expected_geppetto, sizeof(expected_geppetto));
    free(rdram);
    puts(ok ? "legendas_recursos: OK" : "legendas_recursos: FALHOU");
    return ok ? 0 : 1;
}
