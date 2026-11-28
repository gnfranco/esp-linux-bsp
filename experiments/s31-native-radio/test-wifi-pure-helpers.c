/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "esp_event_base.h"
ESP_EVENT_DECLARE_BASE(WIFI_EVENT);
int hexstr2bin(const char *, uint8_t *, size_t);
int main(void)
{
    char s[3];
    uint8_t b[3] = {0xaa,0xaa,0xaa};
    for (unsigned i=0;i<256;i++) {
        assert(snprintf(s,sizeof(s),"%02x",i)==2);
        assert(hexstr2bin(s,b+1,1)==0 && b[1]==i && b[0]==0xaa && b[2]==0xaa);
        assert(snprintf(s,sizeof(s),"%02X",i)==2);
        assert(hexstr2bin(s,b+1,1)==0 && b[1]==i && b[0]==0xaa && b[2]==0xaa);
    }
    assert(hexstr2bin("00aF7b",b,3)==0 && b[0]==0 && b[1]==0xaf && b[2]==0x7b);
    assert(hexstr2bin("x0",b,1)==-1 && hexstr2bin("0x",b,1)==-1);
    assert(hexstr2bin("",b,1)==-1 && hexstr2bin("a",b,1)==-1);
    assert(hexstr2bin("",b,0)==0);
    assert(WIFI_EVENT && strcmp(WIFI_EVENT,"WIFI_EVENT")==0);
    puts("S31_WIFI_PURE_HELPERS_PASS");
    return 0;
}
