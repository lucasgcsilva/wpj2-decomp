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

static size_t read_n64(uint8_t* rdram, uint32_t phys, uint8_t* data, size_t capacity) {
    size_t n = 0;
    while (n + 1u < capacity) {
        data[n] = rdram[(phys + (uint32_t)n) ^ 3u];
        if (!data[n]) return n;
        n++;
    }
    data[n] = 0;
    return n;
}

static size_t normalize_layout(const uint8_t* in, uint8_t* out, size_t capacity) {
    size_t written = 0;
    int pending_space = 0;
    for (size_t i = 0; in[i] && written + 1u < capacity; i++) {
        uint8_t b = in[i];
        if (b == '\n' || b == '\r' || b == ' ') {
            pending_space = written != 0;
            continue;
        }
        if (pending_space && written + 2u < capacity) out[written++] = ' ';
        pending_space = 0;
        out[written++] = b;
        if (b >= 0xE0u && b <= 0xE2u && in[i + 1u] && written + 1u < capacity)
            out[written++] = in[++i];
    }
    out[written] = 0;
    return written;
}

static int layout_valido(const uint8_t* text) {
    unsigned linha = 0;
    for (size_t i = 0; text[i]; i++) {
        if (text[i] >= 0xE0u && text[i] <= 0xE2u && text[i + 1u]) {
            i++;
        } else if (text[i] == '\n' || text[i] == '\r') {
            linha = 0;
        } else if (++linha > 38u) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static const uint8_t source_title[] = "The Siliconian Empire\n";
    static const uint8_t expected_title[] = "O Imp\x2A" "rio Siliconiano";
    static const uint8_t source_bird[] = {
        0xE2, 0x02, 'B','i','r','d', 0xE2, 0x02,
        ':',' ','T','h','e',' ','d','o','c','t','o','r',' ','w','a','n','t','s',' ',
        't','o',' ','s','e','e',' ','y','o','u','!','\n',0
    };
    static const uint8_t expected_bird[] = {
        0xE2, 0x02, 'B','i','r','d', 0xE2, 0x02,
        ':',' ','O',' ','d','o','u','t','o','r',' ','q','u','e','r',' ','v','e','r',' ',
        'v','o','c',0x2B,'!',0
    };
    static const uint8_t source_geppetto[] = {
        0xE2,0x02,'D','r','.', ' ','G','e','p','p','e','t','t','o',0xE2,0x02,':',' ',
        0xE0,0x01,'.','.','.',' ','l','i','s','t','e','n',' ','v','e','r','y','\n',
        'c','a','r','e','f','u','l','l','y','.','.','.','\n',0
    };
    static const uint8_t expected_geppetto[] = {
        0xE2,0x02,'D','r','.', ' ','G','e','p','p','e','t','t','o',0xE2,0x02,':',' ',
        0xE0,0x01,'.','.','.',' ','o','u',0x40,'a',' ','c','o','m','\n',
        'a','t','e','n',0x40,0x25,'o','.','.','.',0
    };
    static const uint8_t source_preformat[] = "It's morning...";
    uint8_t* rdram = (uint8_t*)calloc(1, RDRAM_BYTES);
    if (!rdram) return 2;
    _putenv_s("WPJ2_LEGENDAS", "textos\\traducao_ptbr.tsv");
    _putenv_s("WPJ2_REALOCAR", "1");

    write_n64(rdram, 0x1000, source_title, sizeof(source_title));
    write_n64(rdram, 0x2000, source_bird, sizeof(source_bird));
    write_n64(rdram, 0x3000, source_geppetto, sizeof(source_geppetto));
    write_n64(rdram, 0x4000, source_preformat, sizeof(source_preformat));
    int ok = legendas_substituir_recurso(rdram, 0x80001000u) &&
             legendas_substituir_recurso(rdram, 0x80002000u) &&
             legendas_substituir_recurso(rdram, 0x80003000u) &&
             check_n64(rdram, 0x1000, expected_title, sizeof(expected_title)) &&
             check_n64(rdram, 0x2000, expected_bird, sizeof(expected_bird)) &&
             check_n64(rdram, 0x3000, expected_geppetto, sizeof(expected_geppetto));

    /* O glifo 'ã' usa 0x25, que antes de func_8008EDA4 significa '%'. A rota
     * pré-formatador deve adiar a tradução sem tocar na origem; a rota tardia
     * deve então aplicá-la normalmente. */
    ok = ok &&
         !legendas_substituir_recurso_antes_formatador(
             rdram, 0x80004000u) &&
         check_n64(rdram, 0x4000, source_preformat, sizeof(source_preformat)) &&
         legendas_substituir_recurso(rdram, 0x80004000u);
    if (ok) {
        uint8_t pos_format[64];
        read_n64(rdram, 0x4000, pos_format, sizeof(pos_format));
        ok = strchr((const char*)pos_format, '%') != NULL;
    }

    /* As mensagens abaixo chegam ao formatador ja com os nomes no lugar dos
     * controles E0. A rota composta deve traduzir todos os fragmentos, manter
     * os nomes e publicar uma unica cadeia estavel na arena. */
    static const char* compostos[][2] = {
        { "The Doctor said BEAN-san \x2A de um\nworld I can't see.",
          "O Doutor disse que BEAN-san \x2A de um\nmundo que n\x25o consigo ver." },
        { "Uh, um... Is there a BEAN-san no\nworld which I can't see?",
          "Ah, hum... Existe algum BEAN-san no\nmundo que n\x25o consigo ver?" },
        { "I-I'm Josette e, a partir de hoje, vou\nbe under your care...",
          "E-eu sou Josette e, a partir de hoje, vou\nficar aos seus cuidados..." },
        { "Ah, right! I should explain how to\nse comunicar comigo!",
          "Ah, certo! Eu deveria explicar como\nse comunicar comigo!" },
        { "Ok, I'll begin! Behind me you can see Bird,\nthe Interface Robo.",
          "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23 Bird,\no Rob\x3C de Interface." },
        { "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23 Bird,\no Rob\x3C de Interface.",
          "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23 Bird,\no Rob\x3C de Interface." },
        { "If you'd like to say \xE1\x07\"Yes\"\xE1\xFF or \xE1\x07\"Good\"\xE1\xFF, press\n"
          "the \xE1\x02" "Blue Button\xE1\xFF!",
          "Se quiser dizer \xE1\x07\"Sim\"\xE1\xFF ou \xE1\x07\"Bom\"\xE1\xFF, aperte\n"
          "o \xE1\x02" "Bot\x25o Azul\xE1\xFF!" },
    };
    for (unsigned i = 0; ok && i < sizeof(compostos) / sizeof(compostos[0]); i++) {
        uint32_t origem = 0x5000u + i * 0x200u;
        uint32_t novo = 0;
        write_n64(rdram, origem, (const uint8_t*)compostos[i][0],
                  strlen(compostos[i][0]) + 1u);
        ok = legendas_realocar_recurso_composto(
                 rdram, 0x80000000u | origem, &novo) && novo;
        if (ok) {
            uint8_t obtido[1024];
            uint8_t obtido_norm[1024], esperado_norm[1024];
            uint32_t novo_phys = novo & 0x1FFFFFFFu;
            size_t obtido_n = read_n64(rdram, novo_phys, obtido, sizeof(obtido));
            normalize_layout(obtido, obtido_norm, sizeof(obtido_norm));
            normalize_layout((const uint8_t*)compostos[i][1], esperado_norm,
                             sizeof(esperado_norm));
            ok = novo_phys >= 0x00400000u && novo_phys < RDRAM_BYTES &&
                 obtido_n != 0 && layout_valido(obtido) &&
                 !strcmp((const char*)obtido_norm,
                         (const char*)esperado_norm);
            if (!ok)
                fprintf(stderr, "composto %u divergiu: <%s>\n", i, obtido);
        }
    }
    free(rdram);
    puts(ok ? "legendas_recursos: OK" : "legendas_recursos: FALHOU");
    return ok ? 0 : 1;
}
