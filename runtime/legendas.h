#ifndef WPJ2_LEGENDAS_H
#define WPJ2_LEGENDAS_H

#include <stdint.h>
#include <stddef.h>

void legendas_aplicar_cartucho(uint8_t* cart, size_t rom_size);
void legendas_capturar_rdram(uint8_t* rdram, const char* directory, unsigned id);
/* Substitui em memoria um recurso textual que o carregador acabou de copiar.
 * O endereco permanece o mesmo: assim o formatador, a fonte e a digitacao do
 * jogo continuam sendo os responsaveis pela exibicao. */
int legendas_substituir_recurso(uint8_t* rdram, uint32_t pointer);
/* Rotas que ainda atravessarão func_8008EDA4 devem usar estas variantes.
 * O glifo 'ã', que depois da formatação ocupa o mesmo byte de '%', recebe uma
 * representação transitória segura nesta fase. */
int legendas_substituir_recurso_antes_formatador(uint8_t* rdram,
                                                 uint32_t pointer);
/* Variante para uma copia cujo tamanho do slot e conhecido pelo DMA. O limite
 * inclui o terminador NUL e permite usar a capacidade real mesmo quando os
 * bytes posteriores vieram preenchidos com o recurso seguinte da ROM. */
int legendas_substituir_recurso_com_capacidade(uint8_t* rdram, uint32_t pointer,
                                               uint32_t slot_bytes);
/* Traduz cadeias NUL completas contidas num bloco de recurso recém-carregado.
 * Só altera chaves exatas do catálogo e respeita a capacidade até o próximo
 * dado, portanto serve para tabelas de menu sem desenhar por cima da imagem. */
void legendas_substituir_bloco_estatico(uint8_t* rdram, uint32_t pointer,
                                        uint32_t bytes);
/* Igual a legendas_substituir_recurso, mas autoriza a traducao a NAO caber:
 * nesse caso ela vai para um slot em RDRAM acima dos 4 MB que o jogo enxerga e
 * `novo_ponteiro` recebe o endereco virtual do slot, para o chamador reapontar
 * quem consome o recurso. Devolve 0 sem tocar em nada quando a entrada nao esta
 * no catalogo. Experimental, exige WPJ2_REALOCAR=1. */
int legendas_realocar_recurso(uint8_t* rdram, uint32_t pointer,
                              uint32_t* novo_ponteiro);
int legendas_realocar_recurso_antes_formatador(uint8_t* rdram,
                                               uint32_t pointer,
                                               uint32_t* novo_ponteiro);
/* Converte a representação transitória de 'ã' no glifo definitivo depois de
 * func_8008EDA4, sem procurar novamente a frase no catálogo. */
int legendas_finalizar_pos_formatador(uint8_t* rdram, uint32_t pointer);
/* Registra uma cadeia realmente entregue ao formatador quando ela nao possui
 * chave inglesa nem forma PT-BR reconhecida. A sonda e opt-in pelo caminho em
 * WPJ2_LEGENDAS_AUSENTES_LOG e deduplica frases durante toda a execucao. */
void legendas_auditar_texto_consumido(uint8_t* rdram, uint32_t pointer,
                                      uint32_t caller, int reconhecido);
/* Compara o snapshot anterior a traducao com o ponteiro final entregue ao
 * formatador. Para uma chave exata, valida os bytes finais contra o PT-BR
 * esperado; mensagens compostas ficam explicitamente marcadas como tal. */
void legendas_auditar_correspondencia(uint8_t* rdram,
                                      uint32_t source_pointer,
                                      const uint8_t* source_snapshot,
                                      size_t source_bytes,
                                      uint32_t printed_pointer,
                                      uint32_t caller, int reconhecido);
/* Preserva quebras explicitas. Sem quebra no recurso, antecipa para o espaco
 * anterior somente quando o limite nativo cairia dentro de uma palavra. */
size_t legendas_ajustar_quebra_automatica(unsigned char* text, size_t length);
/* Traduz mensagens formadas por varios fragmentos do catalogo, separados por
 * controles E0/E1/E2 e quebras de linha. Sempre publica uma copia na arena e
 * preserva o recurso estrutural original. */
int legendas_realocar_recurso_composto(uint8_t* rdram, uint32_t pointer,
                                       uint32_t* novo_ponteiro);
/* Contadores da arena, para o veredito nao depender do que aparece na tela. */
void legendas_relatorio_arena(void);
/* Reconstroi, no banco de objetos da fonte ASCII instalada pelo patch Ryu,
 * o glifo acentuado que sera consumido imediatamente por func_80094230. */
int legendas_compor_glifo_ryu(uint8_t* rdram, uint32_t indice_objeto);

#endif
