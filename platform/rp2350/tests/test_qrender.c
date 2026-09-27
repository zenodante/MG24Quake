#include "qrender.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static qr_renderer_t renderer;
static uint8_t frame[QR_WIDTH*QR_HEIGHT+32], previous[QR_WIDTH*QR_HEIGHT];
static uint8_t *load(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *data=malloc((size_t)n);assert(data);assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return data;
}
int main(int argc,char **argv) {
    assert(argc==3);size_t size;uint8_t *image=load(argv[1],&size);qpak_t pak;qpak_cache_t cache={0};
    assert(qpak_open(&pak,image,size));qpak_file_t file;uint8_t palette[768];
    assert(qpak_find(&pak,"gfx/palette.lmp",&file));assert(qpak_read(&pak,&file,&cache,0,palette,sizeof palette));
    const char *maps[]={"start","e1m1","e1m2","e1m3","e1m4","e1m5","e1m6","e1m7","e1m8"};
    for(unsigned i=0;i<9;++i){char name[80];snprintf(name,sizeof name,"maps/%s.bsp",maps[i]);qbsp_t world;
        assert(qbsp_open(&world,&pak,name,&cache));assert(qr_init(&renderer,&world));qr_camera_t camera;
        assert(qr_spawn_camera(&world,&camera));
        if(!i){assert(camera.position[0]==544 && camera.position[1]==288 && camera.position[2]==54 && camera.yaw==90);}
        unsigned drawn=0;
        for(unsigned view=0;view<4;++view){memset(frame,0xa5,sizeof frame);
            bool ok=qr_draw(&renderer,&camera,frame);
            printf("%s yaw=%.0f faces=%u triangles=%u pixels=%u rejected=%u\n",maps[i],camera.yaw,renderer.stats.faces,
                renderer.stats.triangles,renderer.stats.pixels,renderer.stats.rejected);
            assert(ok);assert(renderer.stats.pixels>1000);drawn+=renderer.stats.pixels;
            for(unsigned j=QR_WIDTH*QR_HEIGHT;j<sizeof frame;++j)assert(frame[j]==0xa5);
            if(!view){memcpy(previous,frame,sizeof previous);assert(qr_draw(&renderer,&camera,frame));assert(!memcmp(previous,frame,sizeof previous));
                snprintf(name,sizeof name,"%s/%s.ppm",argv[2],maps[i]);FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n320 152\n255\n");
                for(unsigned p=0;p<QR_WIDTH*QR_HEIGHT;++p)assert(fwrite(palette+frame[p]*3,1,3,f)==3);fclose(f);}
            camera.yaw+=90;
        }
        assert(drawn>10000);
        // Near-plane clipping and PVS boundaries during a reproducible camera sweep.
        qr_camera_t spawn;assert(qr_spawn_camera(&world,&spawn));
        for(unsigned step=0;step<32;++step){camera=spawn;camera.yaw=(float)(step*37);
            camera.position[0]+=((int)(step%7)-3)*16;
            camera.position[1]+=((int)(step%9)-4)*16;
            camera.position[2]+=((int)(step%5)-2)*8;
            assert(qr_draw(&renderer,&camera,frame));
            for(unsigned j=QR_WIDTH*QR_HEIGHT;j<sizeof frame;++j)assert(frame[j]==0xa5);
        }
        camera.yaw=NAN;assert(!qr_draw(&renderer,&camera,frame));
        camera.yaw=90;camera.position[0]=INFINITY;assert(!qr_draw(&renderer,&camera,frame));
    }
    printf("PASS: nine spawn views, 36 view directions plus 288 camera sweeps, frame bounds, deterministic redraw, invalid cameras; renderer workspace=%zu\n",sizeof renderer);
    free(image);return 0;
}
