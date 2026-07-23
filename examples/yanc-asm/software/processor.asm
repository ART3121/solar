NOP
#PRNAME sapho_demo
#NUBITS 32
#NDSTAC 128
#SDEPTH 128
#NUIOIN 1
#NUIOOU 1
#NBMANT 23
#NBEXPO 8
#NUGAIN 128
JMP main
@add NOP
POP
SET_P add_b
SET add_a
ADD add_b
RET
RET
@main NOP
LOD 0
SET main_s
LOD 1
SET main_i
@Lfor_top1 NOP
LOD main_i
P_LOD 10
S_GRE
LIN
JIZ Lfor_end3
LOD main_s
P_LOD main_i
PSH
CAL add
SET main_s
@Lfor_cont2 NOP
LOD main_i
P_LOD 1
S_ADD
SET main_i
JMP Lfor_top1
@Lfor_end3 NOP
LOD main_s
OUT 0
LOD 1
OUT 0
@fim JMP fim
