// SPDX-License-Identifier: Apache-2.0
__attribute__((noinline)) float abi_mix(float x,unsigned scale,float y,float z)
{
    return x*y+z+(float)scale;
}
__attribute__((noinline)) float abi_sum9(float a,float b,float c,float d,float e,
                                        float f,float g,float h,float i)
{
    return a+b+c+d+e+f+g+h+i;
}
__attribute__((noinline)) float abi_sum17(float a,float b,float c,float d,float e,float f,float g,float h,float i,float j,float k,float l,float m,float n,float o,float p,float q)
{
    return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q;
}
