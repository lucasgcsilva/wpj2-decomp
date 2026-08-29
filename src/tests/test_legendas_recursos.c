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
    static const uint8_t source_animated[] = {
        'D','o','n','\'','t',' ','b','e',' ','s','o',' ',
        0xE2,0x06,0x80,0x10,
        's','e','l','.','.','.','f','i','s','h','.','\n',0
    };
    static const uint8_t source_name_second_line[] = {
        0xE2,0x02,'D','r','.', ' ','G','e','p','p','e','t','t','o',0xE2,0x02,':',' ',
        'H','o','h',' ','h','o','h','.','.','.',' ','d','o','n','\'','t',' ','w','o','r','r','y',',','\n',
        0xE0,0x01,'.','.','.','\n',0
    };
    uint8_t* rdram = (uint8_t*)calloc(1, RDRAM_BYTES);
    if (!rdram) return 2;
    CreateDirectoryA("temp", NULL);
    CreateDirectoryA("temp\\projeto", NULL);
    DeleteFileA("temp\\projeto\\test_traducao_ausentes.tsv");
    _putenv_s("WPJ2_LEGENDAS", "textos\\traducao_ptbr.tsv");
    _putenv_s("WPJ2_LEGENDAS_AUSENTES_LOG",
              "temp\\projeto\\test_traducao_ausentes.tsv");
    _putenv_s("WPJ2_REALOCAR", "1");
    int ok = 1;

    /* Quebra explicita e byte estrutural: precisa permanecer exatamente onde
     * estava. Sem quebra, o mesmo comprimento deve ser mantido e somente o
     * espaco anterior ao corte nativo vira newline. */
    {
        unsigned char explicita[] =
            "Esta linha possui uma quebra que precisa\nficar exatamente aqui.";
        unsigned char copia[sizeof(explicita)];
        memcpy(copia, explicita, sizeof(copia));
        legendas_ajustar_quebra_automatica(explicita,
                                            sizeof(explicita) - 1u);
        ok = !memcmp(explicita, copia, sizeof(copia));

        unsigned char automatica[] =
            "123456789012345678901234567890 palavramaislonga depois";
        size_t auto_n = sizeof(automatica) - 1u;
        legendas_ajustar_quebra_automatica(automatica, auto_n);
        ok = ok && auto_n == sizeof(automatica) - 1u &&
             strstr((const char*)automatica, "890\npalavra") != NULL;
        if (!ok) fprintf(stderr, "quebra automatica: <%s>\n", automatica);
    }

    write_n64(rdram, 0x1000, source_title, sizeof(source_title));
    write_n64(rdram, 0x2000, source_bird, sizeof(source_bird));
    write_n64(rdram, 0x3000, source_geppetto, sizeof(source_geppetto));
    write_n64(rdram, 0x4000, source_preformat, sizeof(source_preformat));
    write_n64(rdram, 0x4800, source_animated, sizeof(source_animated));
    write_n64(rdram, 0x4C00, source_name_second_line,
              sizeof(source_name_second_line));

    /* Menus como a loja chegam como tabelas de pequenos slots NUL. A
     * traducao deve alterar o recurso carregado, respeitando a capacidade do
     * slot, e nunca desenhar uma segunda legenda sobre o framebuffer. */
    {
        static const uint8_t shop_block[32] = {
            'B','u','y',0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
            'B','a','c','k',0, 0,0,0, 0,0,0,0, 0,0,0,0
        };
        static const uint8_t comprar[] = "Comprar";
        static const uint8_t voltar[] = "Voltar";
        write_n64(rdram, 0x6000u, shop_block, sizeof(shop_block));
        legendas_substituir_bloco_estatico(rdram, 0x80006000u,
                                            sizeof(shop_block));
        ok = ok && check_n64(rdram, 0x6000u, comprar, sizeof(comprar)) &&
             check_n64(rdram, 0x6010u, voltar, sizeof(voltar));

        static const uint8_t desc[] =
            "This oil recharges a Gijin's physical\nstrength.";
        uint8_t desc_slot[128] = {0}, desc_got[128] = {0};
        memcpy(desc_slot, desc, sizeof(desc));
        write_n64(rdram, 0x6100u, desc_slot, sizeof(desc_slot));
        legendas_substituir_bloco_estatico(rdram, 0x80006100u,
                                            sizeof(desc_slot));
        read_n64(rdram, 0x6100u, desc_got, sizeof(desc_got));
        ok = ok && !memcmp(desc_got, "Este ", 5u) &&
             strstr((const char*)desc_got, "This oil") == NULL;
        if (!ok) fprintf(stderr, "bloco estatico da loja divergiu\n");
    }
    int stage = 1;
    ok = ok && legendas_substituir_recurso(rdram, 0x80001000u) &&
             legendas_substituir_recurso(rdram, 0x80002000u) &&
             legendas_substituir_recurso(rdram, 0x80003000u) &&
             check_n64(rdram, 0x1000, expected_title, sizeof(expected_title)) &&
             check_n64(rdram, 0x2000, expected_bird, sizeof(expected_bird)) &&
             check_n64(rdram, 0x3000, expected_geppetto, sizeof(expected_geppetto));

    /* O glifo 'ã' usa 0x25, que antes de func_8008EDA4 significa '%'. A rota
     * pré-formatador usa 0x7F e a rota tardia o converte para o glifo sem
     * adiar a tradução inteira. */
    if (ok) stage = 2;
    ok = ok &&
         legendas_substituir_recurso_antes_formatador(
             rdram, 0x80004000u);
    if (ok) {
        uint8_t pre_format[64] = {0}, pos_format[64] = {0};
        read_n64(rdram, 0x4000, pre_format, sizeof(pre_format));
        ok = memchr(pre_format, 0x7F, sizeof(pre_format)) != NULL &&
             memchr(pre_format, 0x25, sizeof(pre_format)) == NULL &&
             legendas_finalizar_pos_formatador(rdram, 0x80004000u);
        read_n64(rdram, 0x4000, pos_format, sizeof(pos_format));
        ok = ok && memchr(pos_format, 0x25, sizeof(pos_format)) != NULL &&
             memchr(pos_format, 0x7F, sizeof(pos_format)) == NULL;
        /* Reentrada no mesmo bloco: a forma PT-BR pós-formatador deve ser
         * reconhecida e protegida outra vez, sem depender da chave inglesa. */
        if (ok) {
            memset(pre_format, 0, sizeof(pre_format));
            ok = legendas_substituir_recurso_antes_formatador(
                     rdram, 0x80004000u);
            read_n64(rdram, 0x4000, pre_format, sizeof(pre_format));
            ok = ok && memchr(pre_format, 0x7F, sizeof(pre_format)) != NULL &&
                 memchr(pre_format, 0x25, sizeof(pre_format)) == NULL;
        }
    }

    /* E0 01 no inicio da segunda linha insere Josette. A primeira linha em
     * portugues e maior que a inglesa; o controle deve continuar logo depois
     * da quebra, nunca ser deslocado para dentro de "preocupe". */
    if (ok) stage = 3;
    if (ok) {
        uint32_t name_slot = 0;
        uint8_t got[128] = {0};
        int name_result = legendas_realocar_recurso_antes_formatador(
                              rdram, 0x80004C00u, &name_slot);
        ok = name_result;
        if (ok) {
            uint32_t name_phys = name_slot
                               ? name_slot & 0x1FFFFFFFu : 0x4C00u;
            read_n64(rdram, name_phys, got, sizeof(got));
            uint8_t* newline = (uint8_t*)memchr(got, '\n', sizeof(got));
            ok = newline && newline[1] == 0xE0 && newline[2] == 0x01;
            if (!ok) {
                fprintf(stderr, "controle E001 deslocado: <%s>\n", got);
                if (newline)
                    fprintf(stderr, "apos newline: %02X %02X %02X\n",
                            newline[1], newline[2], newline[3]);
            }
        }
    }

    /* E2 06 possui mais dois bytes de payload. A fala inteira deve ser
     * reconhecida, traduzida e conservar os quatro bytes da animação. */
    if (ok) stage = 5;
    if (ok) {
        uint32_t animated_slot = 0;
        ok = legendas_realocar_recurso_antes_formatador(
                 rdram, 0x80004800u, &animated_slot);
        if (ok) {
            uint8_t got[128] = {0};
            uint32_t animated_phys = animated_slot
                                   ? animated_slot & 0x1FFFFFFFu : 0x4800u;
            read_n64(rdram, animated_phys, got, sizeof(got));
            static const uint8_t control[] = {0xE2,0x06,0x80,0x10};
            ok = strstr((const char*)got, "N") == (const char*)got &&
                 memchr(got, 0x7F, sizeof(got)) != NULL;
            int control_found = 0;
            for (size_t i = 0; i + sizeof(control) <= sizeof(got); i++)
                if (!memcmp(got + i, control, sizeof(control))) {
                    control_found = 1;
                    break;
                }
            ok = ok && control_found;
        }
    }

    /* As mensagens abaixo chegam ao formatador ja com os nomes no lugar dos
     * controles E0. A rota composta deve traduzir todos os fragmentos, manter
     * os nomes e publicar uma unica cadeia estavel na arena. */
    if (ok) stage = 5;
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
          "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23\nBird, o Rob\x3C de Interface." },
        { "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23 Bird,\no Rob\x3C de Interface.",
          "Certo, vou come\x40" "ar! Atr\x23s de mim est\x23\nBird, o Rob\x3C de Interface." },
        { "If you'd like to say \xE1\x07\"Yes\"\xE1\xFF or \xE1\x07\"Good\"\xE1\xFF, press\n"
          "the \xE1\x02" "Blue Button\xE1\xFF!",
          "Se quiser dizer \xE1\x07\"Sim\"\xE1\xFF ou \xE1\x07\"Bom\"\xE1\xFF, aperte\n"
          "o \xE1\x02" "Bot\x25o Azul\xE1\xFF!" },
        { "To carry an object around, place Bird over\n"
          "it, then press and hold the \xE1\x07yellow \xE1\xFF button!\n",
          "Para carregar algo, coloque Bird sobre\n"
          "ele e segure o \xE1\x07" "amarelo \xE1\xFF bot\x25o!\n" },
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
            uint32_t novo_phys = novo & 0x1FFFFFFFu;
            size_t obtido_n = read_n64(rdram, novo_phys, obtido, sizeof(obtido));
            ok = novo_phys >= 0x00400000u && novo_phys < RDRAM_BYTES &&
                 obtido_n != 0 &&
                 !strcmp((const char*)obtido, compostos[i][1]);
            if (!ok)
                fprintf(stderr, "composto %u divergiu: <%s>\n", i, obtido);
        }
    }
    if (ok) {
        static const uint8_t unknown_en[] = "Press the green button now.";
        static const uint8_t dynamic_pt[] = "Por favor, pressione o botao.";
        WIN32_FILE_ATTRIBUTE_DATA before = {0}, after = {0};
        write_n64(rdram, 0x7000u, unknown_en, sizeof(unknown_en));
        legendas_auditar_texto_consumido(rdram, 0x80007000u,
                                          0x80001234u, 0);
        ok = GetFileAttributesExA("temp\\projeto\\test_traducao_ausentes.tsv",
                                  GetFileExInfoStandard, &before) &&
             before.nFileSizeLow > 40u;
        write_n64(rdram, 0x7100u, dynamic_pt, sizeof(dynamic_pt));
        legendas_auditar_texto_consumido(rdram, 0x80007100u,
                                          0x80001234u, 0);
        ok = ok && GetFileAttributesExA(
             "temp\\projeto\\test_traducao_ausentes.tsv",
             GetFileExInfoStandard, &after) &&
             after.nFileSizeLow == before.nFileSizeLow;
        if (!ok) fprintf(stderr, "auditoria de traducao ausente divergiu\n");
    }
    if (!ok) fprintf(stderr, "falha no estagio %d\n", stage);
    free(rdram);
    puts(ok ? "legendas_recursos: OK" : "legendas_recursos: FALHOU");
    return ok ? 0 : 1;
}
