// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stddef.h>
extern float abi_mix(float,unsigned,float,float);
extern float abi_sum9(float,float,float,float,float,float,float,float,float);
extern float abi_sum17(float,float,float,float,float,float,float,float,float,float,float,float,float,float,float,float,float);
static volatile float input[17]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
static volatile unsigned scale=7;
static long syscall3(long nr,long x,long y,long z)
{
    register long a0 asm("a0")=x;
    register long a1 asm("a1")=y;
    register long a2 asm("a2")=z;
    register long a7 asm("a7")=nr;
    asm volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a7):"memory");
    return a0;
}
static uint32_t bits(float f)
{
    union { float f; uint32_t u; } v={.f=f};
    return v.u;
}
__attribute__((noreturn)) void _start(void)
{
    /* Separate compilation units force real ILP32F register/stack calls. */
    int bad=bits(abi_mix(input[0],scale,input[1],input[2]))!=0x41400000U;
    bad|=bits(abi_sum9(input[0],input[1],input[2],input[3],input[4],
                       input[5],input[6],input[7],input[8]))!=0x42340000U;
    bad|=bits(abi_sum17(input[0],input[1],input[2],input[3],input[4],input[5],input[6],input[7],input[8],input[9],input[10],input[11],input[12],input[13],input[14],input[15],input[16]))!=0x43190000U;
    static const char pass[]="ILP32F_CALL_ABI_PASS\n";
    static const char fail[]="ILP32F_CALL_ABI_FAIL\n";
    const char *msg=bad?fail:pass;
    size_t n=bad?sizeof(fail)-1:sizeof(pass)-1;
    if(syscall3(64,1,(long)msg,n)!=(long)n) bad=1;
    syscall3(93,!!bad,0,0);
    for(;;) asm volatile("unimp");
}
