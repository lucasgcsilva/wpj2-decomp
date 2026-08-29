"""Produz uma copia instrumentada da saida do recompilador.

Chamadas diretas entre funcoes recompiladas sao chamadas C normais, entao nunca
passam pelo lookup do runtime. Para medir quanto do jogo realmente executa,
copiamos a saida e inserimos um hook no topo de cada corpo de funcao.

A copia e descartavel: `src/RecompiledFuncs/` continua sendo o artefato de
referencia e nunca e modificado no lugar.
"""
import os
import re
import shutil
import sys

SIG = re.compile(
    r"^RECOMP_FUNC void (func_([0-9A-F]{8})|recomp_entrypoint)"
    r"\(uint8_t\* rdram, recomp_context\* ctx\) \{\s*$"
)

ENTRYPOINT_VRAM = 0x80000400

# Estas rotinas do kernel manipulam uma OSThread em varias escritas que precisam
# ser observadas como uma unica secao critica. Em especial 800CC98C grava
# TH_STATE=WAITING e so depois chama __osEnqueueThread; inserir RECOMP_POLL no
# rotulo intermediario permitia que o scheduler visse uma thread esperando ainda
# presa na fila executavel. Elas nao possuem loops de jogo, logo nao precisam de
# um ponto de preempcao cooperativa interno.
NO_POLL_FUNCTIONS = {
    "func_800CC98C",  # salva contexto e insere a thread na fila escolhida
    "func_800C4AA0",  # osSendMesg: atualiza contador/indice/fila de espera
    "func_800C4C40",  # osRecvMesg: consome ou estaciona a thread atomicamente
}

# Rotulo de destino de desvio no C gerado. Todo laco do MIPS vira um `goto` para
# um destes, entao e o unico lugar onde da para interromper um laco que nao
# chama funcao nenhuma - como o laco ocioso da libultra.
LABEL = re.compile(r"^L_[0-9A-F]{8}:\s*$")


def load_overrides(path):
    """Funcoes que o runtime implementa nativamente. O corpo recompilado nao pode
    simplesmente sumir: chamadas diretas entre funcoes recompiladas viram
    chamadas C, entao o simbolo precisa existir uma unica vez - e a versao que
    vale e a do runtime. Renomear o corpo original resolve os dois lados."""
    names = set()
    if path and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#")[0].strip()
                if line:
                    names.add(line)
    return names


def main():
    src, dst = sys.argv[1], sys.argv[2]
    overrides = load_overrides(sys.argv[3] if len(sys.argv) > 3 else None)

    if os.path.exists(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)

    total = 0
    replaced = 0
    labels = 0
    for name in sorted(os.listdir(src)):
        sp = os.path.join(src, name)
        dp = os.path.join(dst, name)
        if not name.endswith(".c"):
            shutil.copyfile(sp, dp)
            continue

        with open(sp, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()

        out = []
        trace_include_inserted = False
        current_function = None
        for line in lines:
            # trace.h também pode redefinir sondas residuais do C gerado. Ele
            # precisa vir depois de stdio.h; antes disso o macro `printf`
            # alteraria a própria declaração da CRT.
            if not trace_include_inserted and not line.startswith("#include "):
                out.append('#include "trace.h"\n')
                trace_include_inserted = True
            m = SIG.match(line)
            if m:
                current_function = m.group(1)
            if m and m.group(1) in overrides:
                out.append(line.replace(m.group(1), m.group(1) + "__replaced", 1))
                replaced += 1
                continue
            out.append(line)
            if m:
                vram = ENTRYPOINT_VRAM if m.group(2) is None else int(m.group(2), 16)
                out.append("    RECOMP_TRACE(0x%08Xu);\n" % vram)
                total += 1
            elif LABEL.match(line):
                if current_function not in NO_POLL_FUNCTIONS:
                    out.append("    RECOMP_POLL();\n")
                    labels += 1

        if not trace_include_inserted:
            out.append('#include "trace.h"\n')

        with open(dp, "w", encoding="utf-8") as f:
            f.writelines(out)

    missing = len(overrides) - replaced
    print("copia instrumentada em %s (%d funcoes com hook, %d pontos de laco,"
          " %d substituidas)" % (dst, total, labels, replaced))
    if missing:
        print("AVISO: %d override(s) sem corpo correspondente na saida" % missing)


if __name__ == "__main__":
    main()
