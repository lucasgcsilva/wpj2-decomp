# Inventário dos executáveis ativos

Revisão feita durante a reorganização de 20/08/2026.

| Entrada | Estado | Observação |
|---|---|---|
| `wpj2_visual.exe` | preservado | executável visual principal |
| `wpj2_probe.exe` | preservado | sonda usada por `JOGAR.bat` e `RODAR.bat` |
| `build/wpj2_audio_rsp_exato.exe` | preservado | perfil padrão atual de `TESTAR.bat` |
| `RODAR_PROTOTYPE.bat` | referência ausente | já apontava para `prototipos/wpj2_proto_v0.2_abertura3d_20260811.exe`, que não estava no projeto antes da reorganização |

Arquivos `.obj`, `.map` e executáveis auxiliares regeneráveis foram retirados
da árvore ativa. Projetos e binários de terceiros em `tools` não foram
alterados, exceto pelos caminhos de saída dos scripts próprios.
