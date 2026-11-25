#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
static volatile sig_atomic_t running=1;
static void stop(int n){(void)n;running=0;}
static int put(const char *path,const char *s){int f=open(path,O_WRONLY);if(f<0)return -1;int n=write(f,s,strlen(s));int e=errno;close(f);errno=e;return n==(int)strlen(s)?0:-1;}
int main(void){const char *b="/sys/class/leds/s31:rgb:status/brightness",*p="/sys/class/leds/s31:rgb:status/multi_intensity";char buf[40];const int c[7][3]={{255,0,0},{255,255,0},{0,255,0},{0,255,255},{0,0,255},{255,0,255},{255,0,0}};signal(SIGTERM,stop);signal(SIGINT,stop);if(put(b,"48\n")){perror("brightness");return 1;}while(running)for(int k=0;k<6&&running;k++)for(int i=0;i<50&&running;i++){int r=c[k][0]+(c[k+1][0]-c[k][0])*i/50,g=c[k][1]+(c[k+1][1]-c[k][1])*i/50,bl=c[k][2]+(c[k+1][2]-c[k][2])*i/50;snprintf(buf,sizeof(buf),"%d %d %d\n",r,g,bl);if(put(p,buf)){perror("RGB write");put(b,"0\n");return 2;}usleep(20000);}put(b,"0\n");return 0;}
