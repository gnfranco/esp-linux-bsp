/* SPDX-License-Identifier: Apache-2.0 */
    .text
    .p2align 2
    .globl bad_rt_sigreturn
    .type bad_rt_sigreturn,@function
bad_rt_sigreturn:
    mv sp, a0
    li a7, 139 /* __NR_rt_sigreturn, RV32 asm-generic */
    ecall
    unimp
    .size bad_rt_sigreturn, .-bad_rt_sigreturn
