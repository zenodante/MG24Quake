#include "qcollision.h"
#include "qrender.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static qc_workspace_t workspace;
static unsigned reference_hull=1;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int i16(const uint8_t *p){return (int16_t)(p[0]|(unsigned)p[1]<<8);}
static float f32(const uint8_t *p){uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f;}
static void put32(uint8_t *p,uint32_t v){for(unsigned k=0;k<4;++k)p[k]=(uint8_t)(v>>(8*k));}
static void put16(uint8_t *p,int v){p[0]=(uint8_t)v;p[1]=(uint8_t)((unsigned)v>>8);}
static void putf(uint8_t *p,float f){uint32_t v;memcpy(&v,&f,4);put32(p,v);}
static float dist(const uint8_t *p,const float *v){unsigned type=u32(p+16);return type<3?v[type]-f32(p+12):v[0]*f32(p)+v[1]*f32(p+4)+v[2]*f32(p+8)-f32(p+12);}
static bool closef(float a,float b){return fabsf(a-b)<0.0002f;}
/* Quake's original recursive algorithm, kept independent from the iterative
 * implementation. Host-only stack; validated original maps only. */
static int point(const qbsp_t *b,int n,const float *p){unsigned lump=reference_hull?QBSP_CLIPNODES:QBSP_NODES;unsigned budget=b->lump[lump].count;
    while(n>=0){assert(budget--);const uint8_t *node=qbsp_record(b,reference_hull?QBSP_CLIPNODES:QBSP_NODES,(unsigned)n);assert(node);
        const uint8_t *plane=qbsp_record(b,QBSP_PLANES,u32(node));assert(plane);n=i16(node+4+(dist(plane,p)<0?2:0));}return reference_hull?n:(int32_t)u32(qbsp_record(b,QBSP_LEAVES,(unsigned)(-n-1)));}
static bool recursive(const qbsp_t *b,int root,int n,float af,float bf,const float *a,const float *z,qc_trace_t *t,unsigned depth){
    assert(depth<256);
    if(n<0){if(!reference_hull)n=(int32_t)u32(qbsp_record(b,QBSP_LEAVES,(unsigned)(-n-1)));if(n!=-2){t->allsolid=false;if(n==-1)t->inopen=true;else t->inwater=true;}else t->startsolid=true;return true;}
    const uint8_t *node=qbsp_record(b,reference_hull?QBSP_CLIPNODES:QBSP_NODES,(unsigned)n),*p=qbsp_record(b,QBSP_PLANES,u32(node));
    float d1=dist(p,a),d2=dist(p,z);
    if(d1>=0 && d2>=0)return recursive(b,root,i16(node+4),af,bf,a,z,t,depth+1);
    if(d1<0 && d2<0)return recursive(b,root,i16(node+6),af,bf,a,z,t,depth+1);
    unsigned side=d1<0;float f=(d1+(side?0.03125f:-0.03125f))/(d1-d2);if(f<0)f=0;if(f>1)f=1;
    float midf=af+(bf-af)*f,mid[3];for(unsigned k=0;k<3;++k)mid[k]=a[k]+f*(z[k]-a[k]);
    if(!recursive(b,root,i16(node+4+2*side),af,midf,a,mid,t,depth+1))return false;
    if(point(b,i16(node+4+2*(side^1u)),mid)!=-2)return recursive(b,root,i16(node+4+2*(side^1u)),midf,bf,mid,z,t,depth+1);
    if(t->allsolid)return false;
    for(unsigned k=0;k<3;++k)t->normal[k]=(side?-1:1)*f32(p+4*k);t->distance=(side?-1:1)*f32(p+12);
    while(point(b,root,mid)==-2){f-=0.1f;if(f<0){t->fraction=midf;memcpy(t->end,mid,12);return false;}
        midf=af+(bf-af)*f;for(unsigned k=0;k<3;++k)mid[k]=a[k]+f*(z[k]-a[k]);}
    t->fraction=midf;memcpy(t->end,mid,12);return false;
}
static void analytic(void){
    uint8_t planes[40]={0},nodes[16]={0},models[64]={0};qpak_t pak={0};
    qbsp_t b={.pak=&pak};b.lump[QBSP_PLANES]=(qbsp_lump_t){.mapped=planes,.count=2};
    b.lump[QBSP_CLIPNODES]=(qbsp_lump_t){.mapped=nodes,.count=2};b.lump[QBSP_MODELS]=(qbsp_lump_t){.mapped=models,.count=1};
    putf(planes,1);put16(nodes+4,-1);put16(nodes+6,-2);
    float a[3]={10,0,0},z[3]={-10,10,0};qc_trace_t t;
    assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(closef(t.fraction,(10-0.03125f)/20));
    assert(closef(t.end[0],0.03125f)&&t.normal[0]==1&&!t.startsolid&&!t.allsolid);
    float pos[3]={10,0,0},move[3]={-20,10,0};assert(qc_slide(&workspace,&b,pos,move));assert(closef(pos[0],0.03125f)&&closef(pos[1],10));
    a[0]=-10;z[0]=-5;assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(t.startsolid&&t.allsolid);
    z[0]=10;assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(t.startsolid&&!t.allsolid&&t.fraction==1);
    a[0]=10;z[0]=10;assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(t.fraction==1&&!t.startsolid);
    // Thin slab between X=0 and X=1: both endpoints are empty.
    putf(planes+12,1);putf(planes+20,1);put32(nodes+8,1);put16(nodes+6,1);put16(nodes+12,-2);put16(nodes+14,-1);
    a[0]=10;z[0]=-10;assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(closef(t.end[0],1.03125f));
    a[0]=-10;z[0]=10;assert(qc_trace(&workspace,&b,0,1,a,z,&t));assert(closef(t.end[0],-0.03125f)&&t.normal[0]==-1);
    // Two perpendicular walls, simultaneous corner impact.
    putf(planes+12,0);put16(nodes+4,1);put16(nodes+6,-2);putf(planes+20,0);putf(planes+24,1);put32(planes+36,1);
    put16(nodes+12,-1);put16(nodes+14,-2);pos[0]=pos[1]=10;move[0]=move[1]=-20;
    assert(qc_slide(&workspace,&b,pos,move));assert(pos[0]>=0&&pos[1]>=0&&pos[0]<0.04f&&pos[1]<0.04f);
    // Cyclic data must fail closed; invalid inputs must not move the caller.
    put16(nodes+4,0);a[0]=z[0]=10;assert(!qc_trace(&workspace,&b,0,1,a,z,&t));assert(t.allsolid&&t.fraction==0&&t.end[0]==10);
    float save[3];memcpy(save,pos,12);move[0]=NAN;assert(!qc_slide(&workspace,&b,pos,move));assert(!memcmp(save,pos,12));
    assert(!qc_trace(&workspace,&b,0,4,a,z,&t));assert(!qc_trace(&workspace,&b,1,1,a,z,&t));
}
int main(int argc,char **argv){assert(argc==2);analytic();FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
    uint8_t *data=malloc((size_t)size);assert(data);assert(fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);qpak_t pak;qpak_cache_t cache={0};assert(qpak_open(&pak,data,(size_t)size));
    unsigned queries=0,impacts=0,rejected=0;uint32_t rng=71;const char *names[]={"start","e1m1","e1m2","e1m3","e1m4","e1m5","e1m6","e1m7","e1m8"};
    for(unsigned m=0;m<9;++m){char path[40];snprintf(path,sizeof path,"maps/%s.bsp",names[m]);qbsp_t b;assert(qbsp_open(&b,&pak,path,&cache));qr_camera_t camera;assert(qr_spawn_camera(&b,&camera));camera.position[2]-=22;
        for(unsigned hull=0;hull<=3;++hull){reference_hull=hull;int root=(int32_t)u32(qbsp_record(&b,QBSP_MODELS,0)+36+4*hull);
            for(unsigned i=0;i<2000;++i){float a[3],z[3];for(unsigned k=0;k<3;++k){rng=rng*1664525u+1013904223u;a[k]=camera.position[k]+((int)(rng%1025)-512);
                    rng=rng*1664525u+1013904223u;z[k]=a[k]+((int)(rng%4097)-2048);}
                qc_trace_t got,ref={.fraction=1,.allsolid=true};memcpy(ref.end,z,12);recursive(&b,root,root,0,1,a,z,&ref,0);
                if(!qc_trace(&workspace,&b,0,hull,a,z,&got)) {
                    // Quake can back up past the segment start after beginning
                    // inside solid. The target deliberately fails closed here.
                    assert(ref.startsolid && !ref.allsolid);
                    assert(got.allsolid && got.fraction==0 && !memcmp(got.end,a,12));
                    ++rejected;continue;
                }
                assert(closef(got.fraction,ref.fraction));assert(got.startsolid==ref.startsolid&&got.allsolid==ref.allsolid&&got.inopen==ref.inopen&&got.inwater==ref.inwater);
                for(unsigned k=0;k<3;++k){assert(closef(got.end[k],ref.end[k]));assert(closef(got.normal[k],ref.normal[k]));}
                if(!got.startsolid && !got.allsolid && got.fraction<1){int32_t contents;assert(qbsp_point_contents(&b,0,hull,got.end,&contents));assert(contents!=-2);++impacts;}
                ++queries;
            }
        }
        float pos[3];memcpy(pos,camera.position,12);int32_t contents;
        assert(qbsp_point_contents(&b,0,1,pos,&contents) && contents!=-2);
        for(unsigned step=0;step<1000;++step){float delta[3],saved[3];memcpy(saved,pos,12);
            for(unsigned k=0;k<3;++k){rng=rng*1664525u+1013904223u;delta[k]=(int)(rng%81)-40;}
            bool ok=qc_slide(&workspace,&b,pos,delta);
            if(!ok)assert(!memcmp(saved,pos,12));
            assert(qbsp_point_contents(&b,0,1,pos,&contents) && contents!=-2);
        }
        printf("PASS collision %s\n",names[m]);
    }
    printf("PASS: %u swept traces match original recursive Quake; %u safe impact endpoints; %u embedded-start numerical failures safely rejected; analytic wall/slab/corner/slide/error cases; scratch=%zu\n",queries,impacts,rejected,sizeof workspace);
    free(data);return 0;
}
