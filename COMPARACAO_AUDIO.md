# Comparacao de audio com o oraculo do Project64

Tres capturas da mesma cena, 25 s cada, mudando so o que acontece quando a
fila de saida do host enche. O oraculo e `wpj2_audio_oracle.wav`.

A pergunta: a correlacao **para de cair** depois dos primeiros segundos?

## sem_espera — descarta na hora (comportamento antigo)

| tempo | deslocamento | correlacao |
|---:|---:|---:|

Media da correlacao: **0.000** antes de 10 s, **0.000** depois de 12 s.

## espera12 — espera ate 12 ms (novo padrao)

| tempo | deslocamento | correlacao |
|---:|---:|---:|

Media da correlacao: **0.000** antes de 10 s, **0.000** depois de 12 s.

## espera40 — espera ate 40 ms

| tempo | deslocamento | correlacao |
|---:|---:|---:|

Media da correlacao: **0.000** antes de 10 s, **0.000** depois de 12 s.

## Resumo

| corrida | antes de 10 s | depois de 12 s | queda |
|---|---:|---:|---:|
| sem_espera | 0.000 | 0.000 | 0% |
| espera12 | 0.000 | 0.000 | 0% |
| espera40 | 0.000 | 0.000 | 0% |

Se `sem_espera` cair muito e as outras nao, a causa do chiado era o descarte
de buffers. Se as tres cairem igual, era outra coisa e o descarte era sintoma.