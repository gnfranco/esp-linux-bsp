// SPDX-License-Identifier: Apache-2.0
.section .text
.option push
.option arch,+f,+zcf
.global fp_misaligned
.type fp_misaligned,@function
fp_misaligned:
    .option push
    .option norvc
    flw ft0,0(a0)
    fsw ft0,0(a1)
    fmv.x.w a0,ft0
    ret
    .option pop
.global fp_misaligned_c
.type fp_misaligned_c,@function
fp_misaligned_c:
    c.flw fa0,0(a0)
    c.fsw fa0,0(a1)
    fmv.x.w a0,fa0
    ret
.option pop
.option push
.option arch,+d
.option norvc
.global fp_forbidden_d
.type fp_forbidden_d,@function
fp_forbidden_d:
    fadd.d ft0,ft0,ft0
    ret
.global fp_forbidden_ld
.type fp_forbidden_ld,@function
fp_forbidden_ld:
    fld ft0,0(a0)
    ret
.global fp_forbidden_sd
.type fp_forbidden_sd,@function
fp_forbidden_sd:
    fsd ft0,0(a0)
    ret
.option pop
.section .note.GNU-stack,"",@progbits
