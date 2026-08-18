#ifndef WPJ2_OS_H
#define WPJ2_OS_H

/* Layout do SO desta ROM, lido instrucao a instrucao de __osDispatchThread
 * (func_800CCAE4, ROM 0xCD6E4). Aquela funcao restaura o contexto registrador
 * por registrador, entao os deslocamentos que ela carrega *sao* a estrutura.
 * Nada aqui foi copiado de outro jogo.
 *
 *   ld $at, 0x20($k0) ... ld $ra, 0x100($k0)   contexto de 64 bits
 *   ld $k1, 0x108($k0) ; mtlo                  lo
 *   ld $k1, 0x110($k0) ; mthi                  hi
 *   lw $k1, 0x118($k0) ; mtc0 Status           sr
 *   lw $k1, 0x11C($k0) ; mtc0 EPC              pc
 *   lw $k1, 0x018($k0) ; beqz -> pula FPU      flag de contexto de FPU
 *   lw $k1, 0x128($k0)                         mascara rcp
 *   sh 4, 0x10($v0)                            estado = OS_STATE_RUNNING
 */

/* --- campos de OSThread --- */
#define TH_NEXT        0x000
#define TH_PRIORITY    0x004
#define TH_QUEUE       0x008
#define TH_STATE       0x010   /* halfword */
#define TH_FLAGS       0x012   /* halfword */
#define TH_ID          0x014
#define TH_FP          0x018
#define TH_CTX_AT      0x020
#define TH_CTX_A0      0x038
#define TH_CTX_GP      0x0E8
#define TH_CTX_SP      0x0F0
#define TH_CTX_S8      0x0F8
#define TH_CTX_RA      0x100
#define TH_CTX_LO      0x108
#define TH_CTX_HI      0x110
#define TH_CTX_SR      0x118
#define TH_CTX_PC      0x11C
#define TH_CTX_RCP     0x128
#define TH_CTX_FPCSR   0x12C

/* Registradores salvos sao de 64 bits big-endian; um ponteiro de 32 bits mora
   na metade baixa, quatro bytes adiante. */
#define LO_WORD        4

#define OS_STATE_STOPPED   1
#define OS_STATE_RUNNABLE  2
#define OS_STATE_RUNNING   4
#define OS_STATE_WAITING   8

/* --- globais do SO --- */
#define ADDR_RUN_QUEUE        0x800ECD08u   /* argumento de __osPopThread     */
#define ADDR_RUNNING_THREAD   0x800ECD10u   /* recebe o resultado do pop      */
#define ADDR_GLOBAL_INT_MASK  0x800ECC4Cu
#define ADDR_RCP_IM_TABLE     0x800EFEE0u

/* --- funcoes substituidas nativamente --- */
#define VRAM_DISPATCH_THREAD  0x800CCAE4u
#define VRAM_GET_COUNT        0x800CBBB0u

#endif
