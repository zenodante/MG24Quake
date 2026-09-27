/* Original BSP reference rasterizer for RP2350 integration.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "qrender.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static float f32(const uint8_t *p) { uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f; }
static bool token(const uint8_t **cursor,const uint8_t *end,char *out,size_t cap) {
    const uint8_t *p=*cursor;size_t n=0;
    while(p<end && *p && *p<=32)++p;
    if(p==end || !*p)return false;
    if(*p=='{' || *p=='}') { out[0]=(char)*p++;out[1]=0;*cursor=p;return true; }
    if(*p++!='"')return false;
    while(p<end && *p && *p!='"') { if(n+1>=cap)return false;out[n++]=(char)*p++; }
    if(p==end || *p!='"')return false;
    out[n]=0;*cursor=p+1;return true;
}
bool qr_spawn_camera(const qbsp_t *b,qr_camera_t *camera) {
    if(!b || !camera || !b->lump[QBSP_ENTITIES].mapped)return false;
    const uint8_t *p=b->lump[QBSP_ENTITIES].mapped,*end=p+b->lump[QBSP_ENTITIES].size;
    char key[128],value[128];
    while(token(&p,end,key,sizeof key)) {
        if(strcmp(key,"{"))return false;
        bool spawn=false,origin=false,closed=false;qr_camera_t c={{0,0,0},0};
        while(token(&p,end,key,sizeof key)) {
            if(!strcmp(key,"}")){closed=true;break;}
            if(!token(&p,end,value,sizeof value))return false;
            if(!strcmp(key,"classname"))spawn=!strcmp(value,"info_player_start");
            if(!strcmp(key,"origin")) {
                char *v=value,*next;
                origin=true;
                for(unsigned k=0;k<3;++k){c.position[k]=strtof(v,&next);if(next==v || !isfinite(c.position[k]))origin=false;v=next;}
            }
            if(!strcmp(key,"angle")){char *next;c.yaw=strtof(value,&next);if(next==value || !isfinite(c.yaw))return false;}
        }
        if(!closed)return false;
        if(spawn && origin){c.position[2]+=22;*camera=c;return true;}
    }
    return false;
}
bool qr_init(qr_renderer_t *r,const qbsp_t *b) {
    if(!r)return false;
    memset(r,0,sizeof *r);r->texture_id=-1;
    if(!b || !b->pak || b->lump[QBSP_FACES].count>QR_MAX_FACES ||
       b->lump[QBSP_LEAVES].count>QR_MAX_LEAVES)return false;
    qpak_file_t cmap;
    if(!qpak_find(b->pak,"gfx/colormap.lmp",&cmap) || cmap.size<sizeof r->colormap ||
       !qpak_read(b->pak,&cmap,&r->cache,0,r->colormap,sizeof r->colormap))return false;
    r->world=b;r->ready=true;return true;
}
static float distance_to_plane(qr_vertex_t v,unsigned p) {
    switch(p){case 0:return v.z-4;case 1:return v.z+v.x;case 2:return v.z-v.x;
    case 3:return v.z*(QR_HEIGHT/320.0f)+v.y;default:return v.z*(QR_HEIGHT/320.0f)-v.y;}
}
static unsigned clip(const qr_vertex_t *in,unsigned n,qr_vertex_t *out,unsigned plane) {
    unsigned count=0;qr_vertex_t a=in[n-1];float da=distance_to_plane(a,plane);
    for(unsigned i=0;i<n;++i){qr_vertex_t b=in[i];float db=distance_to_plane(b,plane);
        if((da>=0)!=(db>=0)){
            if(count>=QR_POLY_VERTS)return 0;
            float t=da/(da-db);
            out[count++]=(qr_vertex_t){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z),a.s+t*(b.s-a.s),a.t+t*(b.t-a.t)};
        }
        if(db>=0){if(count>=QR_POLY_VERTS)return 0;out[count++]=b;}
        a=b;da=db;
    }return count;
}
static float edge(qr_vertex_t a,qr_vertex_t b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static int wrap(float uv,unsigned size){int v=(int)floorf(uv);int m=v%(int)size;return m<0?m+(int)size:m;}
typedef struct {float smin,tmin;unsigned w,h,styles;} light_t;
static unsigned illumination(const qr_renderer_t *r,light_t l,float s,float t) {
    if(!l.styles)return 0; // Quake colormap row zero is full brightness
    float x=fminf(fmaxf((s-l.smin)/16,0),(float)(l.w-1));
    float y=fminf(fmaxf((t-l.tmin)/16,0),(float)(l.h-1));
    unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<l.w?ix+1:ix,jy=iy+1<l.h?iy+1:iy;
    float sum=0,fx=x-ix,fy=y-iy;
    for(unsigned k=0;k<l.styles;++k){const uint8_t *a=r->light+k*l.w*l.h;
        sum+=(1-fy)*((1-fx)*a[iy*l.w+ix]+fx*a[iy*l.w+jx])+fy*((1-fx)*a[jy*l.w+ix]+fx*a[jy*l.w+jx]);}
    // Match Quake's 8.8 light accumulation to six-bit colormap conversion.
    int level=(int)((255-sum)*0.25f);return level<0?0:level>63?63:(unsigned)level;
}
static void triangle(qr_renderer_t *r,qr_vertex_t a,qr_vertex_t b,qr_vertex_t c,light_t light,uint8_t *pixels) {
    qr_vertex_t v[3]={a,b,c};
    for(unsigned i=0;i<3;++i){float iz=1/v[i].z;v[i].x=160+160*v[i].x*iz;v[i].y=76-160*v[i].y*iz;
        v[i].z=iz;v[i].s*=iz;v[i].t*=iz;}
    a=v[0];b=v[1];c=v[2];float area=edge(a,b,c.x,c.y);
    if(fabsf(area)<0.001f)return;
    if(area<0){qr_vertex_t tmp=b;b=c;c=tmp;area=-area;}
    int x0=(int)fmaxf(0,floorf(fminf(a.x,fminf(b.x,c.x))));
    int x1=(int)fminf(QR_WIDTH-1,ceilf(fmaxf(a.x,fmaxf(b.x,c.x))));
    int y0=(int)fmaxf(0,floorf(fminf(a.y,fminf(b.y,c.y))));
    int y1=(int)fminf(QR_HEIGHT-1,ceilf(fmaxf(a.y,fmaxf(b.y,c.y))));
    ++r->stats.triangles;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float px=x+0.5f,py=y+0.5f;
        float wa=edge(b,c,px,py)/area,wb=edge(c,a,px,py)/area,wc=1-wa-wb;
        if(wa<0 || wb<0 || wc<0)continue;
        float iz=wa*a.z+wb*b.z+wc*c.z;
        uint16_t depth=(uint16_t)fminf(65535,fmaxf(1,iz*262140));unsigned pos=(unsigned)y*QR_WIDTH+(unsigned)x;
        if(depth<=r->depth[pos])continue;
        float s=(wa*a.s+wb*b.s+wc*c.s)/iz,t=(wa*a.t+wb*b.t+wc*c.t)/iz;
        // Keep float-to-int texture addressing bounded even for malformed maps.
        if(!isfinite(s)||!isfinite(t)||fabsf(s)>10000000||fabsf(t)>10000000)continue;
        float scale=1.0f/(1u<<r->mip);
        unsigned texel=r->texture[wrap(t*scale,r->th)*r->tw+wrap(s*scale,r->tw)];
        pixels[pos]=r->colormap[illumination(r,light,s,t)*256+texel];r->depth[pos]=depth;++r->stats.pixels;
    }
}
static bool face(qr_renderer_t *r,uint32_t index,const qr_camera_t *camera,float sy,float cy,uint8_t *pixels) {
    const qbsp_t *b=r->world;const uint8_t *f=qbsp_record(b,QBSP_FACES,index);
    const uint8_t *plane=qbsp_record(b,QBSP_PLANES,u16(f));
    float side=-f32(plane+12);for(unsigned k=0;k<3;++k)side+=camera->position[k]*f32(plane+4*k);
    if((side<0)!=(u16(f+2)!=0))return true;
    unsigned n=u16(f+8);if(n<3 || n>QR_POLY_VERTS-8)return false;
    const uint8_t *ti=qbsp_record(b,QBSP_TEXINFO,u16(f+10));
    float mins[2]={INFINITY,INFINITY},maxs[2]={-INFINITY,-INFINITY};
    for(unsigned i=0;i<n;++i){int32_t e=(int32_t)u32(qbsp_record(b,QBSP_SURFEDGES,u32(f+4)+i));
        const uint8_t *ed=qbsp_record(b,QBSP_EDGES,(uint32_t)(e<0?-(int64_t)e:e));
        const uint8_t *vertex=qbsp_record(b,QBSP_VERTICES,u16(ed+(e<0?2:0)));
        float p[3],uv[2];for(unsigned k=0;k<3;++k){p[k]=f32(vertex+4*k);if(!isfinite(p[k]) || fabsf(p[k])>10000000)return false;}
        for(unsigned j=0;j<2;++j){uv[j]=f32(ti+16*j+12);for(unsigned k=0;k<3;++k)uv[j]+=p[k]*f32(ti+16*j+4*k);
            if(!isfinite(uv[j]) || fabsf(uv[j])>10000000)return false;
            mins[j]=fminf(mins[j],uv[j]);maxs[j]=fmaxf(maxs[j],uv[j]);}
        float dx=p[0]-camera->position[0],dy=p[1]-camera->position[1];
        r->poly[0][i]=(qr_vertex_t){dx*sy-dy*cy,p[2]-camera->position[2],dx*cy+dy*sy,uv[0],uv[1]};
    }
    unsigned slot=0;
    for(unsigned p=0;p<5 && n;++p){n=clip(r->poly[slot],n,r->poly[1-slot],p);slot=1-slot;}
    if(n<3)return true;
    int id=(int)u32(ti+32);
    if(id!=r->texture_id){uint32_t off,w,h;unsigned mip;
        for(mip=0;mip<4;++mip){if(!qbsp_texture_mip(b,(uint32_t)id,mip,&off,&w,&h,&r->cache))return false;
            if((uint64_t)w*h<=sizeof r->texture)break;}
        if(mip==4 || !qbsp_read(b,QBSP_TEXTURES,off,r->texture,w*h,&r->cache))return false;
        r->texture_id=id;r->tw=w;r->th=h;r->mip=mip;
    }
    light_t l={0};
    if((int32_t)u32(f+16)>=0 && !(u32(ti+36)&1)){
        l.smin=floorf(mins[0]/16)*16;l.tmin=floorf(mins[1]/16)*16;
        l.w=(unsigned)(ceilf(maxs[0]/16)-floorf(mins[0]/16))+1;
        l.h=(unsigned)(ceilf(maxs[1]/16)-floorf(mins[1]/16))+1;
        while(l.styles<4 && f[12+l.styles]!=255)++l.styles;
        if(l.w>18 || l.h>18 || !qbsp_read(b,QBSP_LIGHTING,u32(f+16),r->light,l.w*l.h*l.styles,&r->cache))return false;
    }
    ++r->stats.faces;
    for(unsigned i=1;i+1<n;++i)triangle(r,r->poly[slot][0],r->poly[slot][i],r->poly[slot][i+1],l,pixels);
    return true;
}
bool qr_draw(qr_renderer_t *r,const qr_camera_t *c,uint8_t *pixels) {
    if(!r || !r->ready || !c || !pixels || !isfinite(c->yaw))return false;
    for(unsigned k=0;k<3;++k)if(!isfinite(c->position[k]) || fabsf(c->position[k])>10000000)return false;
    const qbsp_t *b=r->world;uint32_t leaf;
    r->stats=(qr_stats_t){0};memset(pixels,0,QR_WIDTH*QR_HEIGHT);memset(r->depth,0,sizeof r->depth);memset(r->visible,0,sizeof r->visible);
    if(!qbsp_point_leaf(b,0,c->position,&leaf) || !qbsp_leaf_pvs(b,leaf,r->pvs,sizeof r->pvs))return false;
    for(uint32_t i=1;i<=b->visleaves;++i)if(r->pvs[(i-1)/8]&(1u<<((i-1)%8))){
        const uint8_t *l=qbsp_record(b,QBSP_LEAVES,i);
        for(unsigned j=0;j<u16(l+22);++j){unsigned f=u16(qbsp_record(b,QBSP_MARKSURFACES,u16(l+20)+j));r->visible[f/8]|=(uint8_t)(1u<<(f%8));}
    }
    const uint8_t *model=qbsp_record(b,QBSP_MODELS,0);uint32_t first=u32(model+56),count=u32(model+60);
    float angle=remainderf(c->yaw,360)*0.01745329252f,sy=sinf(angle),cy=cosf(angle);
    for(uint32_t i=first;i<first+count;++i)if(r->visible[i/8]&(1u<<(i%8)))
        if(!face(r,i,c,sy,cy,pixels))++r->stats.rejected;
    return r->stats.rejected==0;
}
