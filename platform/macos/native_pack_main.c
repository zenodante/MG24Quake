/* Offline resource compiler. This executable does not start SDL. */
#include "mg24_renderer.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"Usage: qnative_pack source.qxip output.qnat\n");return 2;}
    int fd=open(argv[1],O_RDONLY);struct stat st;
    if(fd<0 || fstat(fd,&st) || st.st_size<=0){perror(argv[1]);return 1;}
    void *p=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(p==MAP_FAILED){perror("mmap");return 1;}
    qpak_t pak;bool ok=qpak_open(&pak,p,st.st_size) && mg24_pack(&pak,argv[2]);
    munmap(p,st.st_size);if(!ok)fprintf(stderr,"Native image compilation failed\n");return ok?0:1;
}
