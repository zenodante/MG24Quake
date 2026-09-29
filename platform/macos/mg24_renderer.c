/* MG24 renderer and native-image compiler share the exact engine ABI.
 * The compiler runs OFFLINE. The player only relocates the preexpanded image
 * once at startup, protects it read-only, then switches model entry pointers. */
#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"
#include "mg24_renderer.h"
#undef calloc
#undef free
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static global_data_t state;
global_data_t *const _g=&state;
viddef_t vid;
server_t sv;
entity_t cl_entities[MAX_EDICTS + ADDITIONAL_CLIENT_ENTITIES + MAX_TEMP_ENTITIES];
static client_entity_t world_entity;
/* Compiler scratch is dead-stripped from the player executable. */
static model_t compile_model;
static brush_model_data_t compile_brush;
static model_t *compile_inline;
static brush_model_data_t *compile_inline_brush;
static unsigned compile_models;
model_t *mod_known;
#define brush (*mod_known->brushModelData)
/* Shared scratch storage also holds vertices, edges and native pointers. */
_Alignas(8) uint8_t textureCacheBuffer[MAX_TEXTURE_SIZE];
static const qbsp_t *bound;
static void *binding;
static size_t binding_size;
static byte pvs[(MAX_MAP_LEAFS+7)/8];
static byte dlight_bits[(MAX_MAP_NODES+7)/8];
static byte *texture_source;

byte *r_skysource;
extern uint8_t *nodeHadDlight;
extern int r_outofsurfaces,r_outofedges;
void R_MarkLeaves(void);
void R_EdgeDrawing(void);
void R_SetupFrame(void);
static uint16_t u16(const byte*p){return p[0]|p[1]<<8;}
static int16_t i16(const byte*p){return (int16_t)u16(p);}
static uint32_t u32(const byte*p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static float f32(const byte*p){float f;memcpy(&f,p,4);return f;}
void Sys_Error(char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);abort();}
void Con_Printf(char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);}
void Con_DPrintf(char *fmt,...){(void)fmt;}
void Sbar_Changed(void){}
void S_ExtraUpdate(void){}

byte *Mod_LeafPVS(mleaf_t *leaf,model_t *model){
    (void)model;if(!qbsp_leaf_pvs(bound,(uint32_t)(leaf-brush.leafs),pvs,sizeof pvs))Sys_Error("Bad PVS");return pvs;
}
mleaf_t *Mod_PointInLeaf(float *point,model_t *model){
    (void)model;uint32_t leaf;if(!qbsp_point_leaf(bound,0,point,&leaf))Sys_Error("Bad BSP traversal");return &brush.leafs[leaf];
}
float skyspeed=8,skytime;
int sb_lines=48;
void R_SetSkyFrame(void){skytime=_g->cl.time;}
void R_MakeSky(void){}
byte qmac_sky_sample(const byte *source,int index){
    unsigned x=index&127,y=(unsigned)index>>7,shift=(unsigned)(skytime*skyspeed)&127;
    byte front=source[((y+shift)&127)*256+((x+shift)&127)];
    return front?front:source[y*256+x+128];
}
byte *getCurrentTextureCacheAddress(void){return texture_source;}
void clearTextureCache(void){texture_source=NULL;}
void textureLoaderHandler(byte *last,byte *next,int mip,int pos,int count){(void)last;(void)mip;(void)pos;(void)count;texture_source=next;}
void *getSurfBuffer(msurface_t *s,int pos,int count,int mip,int *width){
    (void)pos;(void)count;r_drawsurf.texture=R_TextureAnimation(s->texinfo->texture);
    for(int i=0;i<4;i++)r_drawsurf.lightadj[i]=s->styles[i]<MAX_LIGHTSTYLES?d_lightstylevalue[s->styles[i]]:0;
    r_drawsurf.surfmip=mip;r_drawsurf.surfwidth=s->extents[0]>>mip;r_drawsurf.surfheight=s->extents[1]>>mip;r_drawsurf.surf=s;
    texture_source=r_drawsurf.texture->extmemdata[mip];
    int n=((s->extents[0]>>4)+1)*((s->extents[1]>>4)+1);
    if(n>sizeof(_g->blocklights)/sizeof(_g->blocklights[0]))Sys_Error("Lightmap overflow");
    for(int i=0;i<n;i++)_g->blocklights[i]=_g->r_refdef.ambientlight<<8;
    if(r_fullbright || !brush.lightdata){for(int i=0;i<n;i++)_g->blocklights[i]=0;}
    else if(s->samples){const byte*p=s->samples;for(int m=0;m<4 && s->styles[m]!=255;m++,p+=n)for(int i=0;i<n;i++)_g->blocklights[i]+=p[i]*r_drawsurf.lightadj[m];}
    R_DrawSurface();*width=r_drawsurf.surfwidth;return texture_source;
}
size_t mg24_binding_bytes(void){return 0;}
size_t mg24_workspace_bytes(void){return sizeof state+sizeof vid+sizeof sv+sizeof cl_entities+sizeof world_entity+sizeof textureCacheBuffer+sizeof pvs+sizeof dlight_bits+sizeof d_zbuffer;}
static void compile_free(void){free(binding);binding=NULL;binding_size=0;}
#define TAKE(field,type,count) do {brush.field=(type*)cursor;cursor+=sizeof(type)*(count);cursor=(byte*)(((uintptr_t)cursor+7)&~(uintptr_t)7);}while(0)
static bool compile_level(const qbsp_t *w){
    compile_free();memset(&compile_brush,0,sizeof compile_brush);memset(&compile_model,0,sizeof compile_model);mod_known=&compile_model;compile_model.brushModelData=&compile_brush;bound=w;if(!w->runtime || w->lump[QBSP_SURFEDGES].stride!=2)return false;
    unsigned np=w->lump[QBSP_PLANES].count,nt=w->lump[QBSP_TEXINFO].count,nf=w->lump[QBSP_FACES].count,nn=w->lump[QBSP_NODES].count,nl=w->lump[QBSP_LEAVES].count,ntex=w->textures;
    if(nn>MAX_MAP_NODES || nn>4096 || nl>MAX_MAP_LEAFS || nf>MAX_SURFACES)return false;
    /* The retained optimized scalar fields have explicit capacity limits. */
    for(unsigned i=0;i<nf;i++){
        const byte*p=qbsp_record(w,QBSP_FACES,i);
        if(u32(p)>INT16_MAX || u16(p+8)>INT8_MAX)return false;
    }
    unsigned nm=w->lump[QBSP_MODELS].count,nc=w->lump[QBSP_CLIPNODES].count;
    if(!nm || nm>255)return false;compile_models=nm;
    binding_size=(nm-1)*(sizeof(model_t)+sizeof(brush_model_data_t))+(nn+nc)*sizeof(dclipnode_t)+32+np*sizeof(mplane_t)+nt*sizeof(mtexinfo_t)+nf*sizeof(msurface_t)+nn*sizeof(mnode_t)+nl*sizeof(mleaf_t)+ntex*(sizeof(texture_t)+sizeof(texture_t*))+64;
    binding=calloc(1,binding_size);if(!binding)return false;byte *cursor=binding;
    compile_inline=(model_t*)cursor;cursor+=(nm-1)*sizeof(model_t);
    compile_inline_brush=(brush_model_data_t*)cursor;cursor+=(nm-1)*sizeof(brush_model_data_t);
    dclipnode_t *hull0=(dclipnode_t*)cursor;cursor+=nn*sizeof(dclipnode_t);
    dclipnode_t *clip=(dclipnode_t*)cursor;cursor+=nc*sizeof(dclipnode_t);
    cursor=(byte*)(((uintptr_t)cursor+7)&~(uintptr_t)7);
    TAKE(planes,mplane_t,np);TAKE(texinfo,mtexinfo_t,nt);TAKE(surfaces,msurface_t,nf);TAKE(nodes,mnode_t,nn);TAKE(leafs,mleaf_t,nl);TAKE(textures,texture_t*,ntex);
    texture_t *textures=(texture_t*)cursor;
    for(unsigned i=0;i<np;i++){const byte*p=qbsp_record(w,QBSP_PLANES,i);mplane_t *o=brush.planes+i;memcpy(o->normal,p,12);setPlaneDist(o,f32(p+12));o->type=p[16];o->signbits=p[17];}
    for(unsigned i=0;i<ntex;i++){texture_t*t=textures+i;brush.textures[i]=t;const byte*p=qbsp_record(w,QBSP_TEXTURES,i);uint32_t width=0,height=0;
        for(int m=0;m<4;m++){t->extmemdata[m]=(byte*)qbsp_texture_pixels(w,i,m,&width,&height);if(!m){t->width=width;t->height=height;}}
        if(u32(p)!=UINT32_MAX && !t->extmemdata[0])return false;
        t->anim_total=u32(p+12);t->anim_min=u32(p+16);t->anim_max=u32(p+20);uint32_t next=u32(p+4),alt=u32(p+8);t->anim_next=next==UINT32_MAX?NULL:textures+next;t->alternate_anims=alt==UINT32_MAX?NULL:textures+alt;
    }
    for(unsigned i=0;i<nt;i++){const byte*p=qbsp_record(w,QBSP_TEXINFO,i);mtexinfo_t*o=brush.texinfo+i;memcpy(o->vecs,p,32);o->texture=brush.textures[u32(p+32)];o->reduced_flags=u32(p+36)&1;o->mipadjust=u32(p+40);}
    for(unsigned i=0;i<nf;i++){const byte*p=qbsp_record(w,QBSP_FACES,i);msurface_t*o=brush.surfaces+i;o->firstedge=u32(p);o->plane=brush.planes+u16(p+4);o->texinfo=brush.texinfo+u16(p+6);o->numedges=u16(p+8);o->flags=u16(p+10);o->surfIdx=i;o->surfNodeIndex=(int32_t)u32(p+28);memcpy(o->texturemins,p+12,4);memcpy(o->extents,p+16,4);memcpy(o->styles,p+20,4);int32_t light=u32(p+24);o->samples=light<0?NULL:(byte*)w->lump[QBSP_LIGHTING].mapped+light;}
    for(unsigned i=0;i<nn;i++){const byte*p=qbsp_record(w,QBSP_NODES,i);mnode_t*o=brush.nodes+i;o->node_idx=i;o->parent_idx=(int32_t)u32(p+24);o->plane=brush.planes+u32(p);memcpy(o->children_idx,p+4,4);memcpy(o->minmaxs,p+8,12);o->firstsurface=u16(p+20);o->numsurfaces=u16(p+22);}
    for(unsigned i=0;i<nl;i++){const byte*p=qbsp_record(w,QBSP_LEAVES,i);mleaf_t*o=brush.leafs+i;o->contents=(int32_t)u32(p);o->parent_idx=(int32_t)u32(p+28);o->leaf_idx=i;memcpy(o->minmaxs,p+8,12);o->firstMarkSurfaceIdx=u16(p+20);o->nummarksurfaces=u16(p+22);memcpy(o->ambient_sound_level,p+24,4);}
    brush.vertexes=(mvertex_t*)w->lump[QBSP_VERTICES].mapped;brush.edges=(medge_t*)w->lump[QBSP_EDGES].mapped;brush.surfedges=(short*)w->lump[QBSP_SURFEDGES].mapped;brush.marksurfaceIdx=(short*)w->lump[QBSP_MARKSURFACES].mapped;
    brush.visdata=(byte*)w->lump[QBSP_VISIBILITY].mapped;brush.lightdata=(byte*)w->lump[QBSP_LIGHTING].mapped;brush.entities=(char*)w->lump[QBSP_ENTITIES].mapped;
    brush.numleafs=w->visleaves;brush.numnodes=nn;brush.numsurfaces=nf;const byte*model=qbsp_record(w,QBSP_MODELS,0);brush.firstmodelsurface=u32(model+56);brush.nummodelsurfaces=u32(model+60);
    for(unsigned i=0;i<nn;i++){
        const byte *p=qbsp_record(w,QBSP_NODES,i);if(u32(p)>INT16_MAX)return false;
        hull0[i].planenum=u32(p);
        for(int j=0;j<2;j++){int child=i16(p+4+2*j);hull0[i].children[j]=child<0?(int32_t)u32(qbsp_record(w,QBSP_LEAVES,-1-child)):child;}
    }
    for(unsigned i=0;i<nc;i++){
        const byte *p=qbsp_record(w,QBSP_CLIPNODES,i);if(u32(p)>INT16_MAX)return false;
        clip[i].planenum=u32(p);clip[i].children[0]=i16(p+4);clip[i].children[1]=i16(p+6);
    }
    brush.numsubmodels=nm;
    for(unsigned h=0;h<MAX_MAP_HULLS;h++){
        hull_t *hull=brush.hulls+h;hull->clipnodes=h?clip:hull0;hull->planes=brush.planes;hull->lastclipnode=(h?nc:nn)-1;
        if(h==1){hull->clip_mins[0]=hull->clip_mins[1]=-16;hull->clip_mins[2]=-24;hull->clip_maxs[0]=hull->clip_maxs[1]=16;hull->clip_maxs[2]=32;}
        if(h==2){hull->clip_mins[0]=hull->clip_mins[1]=-32;hull->clip_mins[2]=-24;hull->clip_maxs[0]=hull->clip_maxs[1]=32;hull->clip_maxs[2]=64;}
    }
    for(unsigned i=0;i<nm;i++){
        model_t *m=i?compile_inline+i-1:&compile_model;
        brush_model_data_t *b=i?compile_inline_brush+i-1:&compile_brush;
        if(i)*b=compile_brush;m->type=mod_brush;m->numframes=2;m->brushModelData=b;
        const byte *p=qbsp_record(w,QBSP_MODELS,i);float radius2=0;
        for(int j=0;j<3;j++){m->mins_s[j]=f32(p+4*j)-1;m->maxs_s[j]=f32(p+12+4*j)+1;float extent=fmaxf(fabsf(m->mins_s[j]),fabsf(m->maxs_s[j]));radius2+=extent*extent;}
        m->radius=sqrtf(radius2);b->numleafs=u32(p+52);b->firstmodelsurface=u32(p+56);b->nummodelsurfaces=u32(p+60);
        for(unsigned h=0;h<MAX_MAP_HULLS;h++)b->hulls[h].firstclipnode=(int32_t)u32(p+36+4*h);
    }
    return true;
}
static bool setup_level(const qbsp_t *w,model_t *model){
    bound=w;mod_known=model;
    memset(&state,0,sizeof state);_g->cl.worldmodel=model;_g->currententity=&cl_entities[0];cl_entities[0].data=&world_entity;world_entity.modelIdx=0;
    sv.active=true;nodeHadDlight=dlight_bits;memset(dlight_bits,0,sizeof dlight_bits);
    qpak_file_t cmap;if(!qpak_find(w->pak,"gfx/colormap.lmp",&cmap))return false;vid.colormap=(byte*)qpak_map(w->pak,&cmap,0,cmap.size);

    viewsize=100;fov=90;r_drawentities=0;r_waterwarp=0;_g->r_refdef.xOrigin=.5f;_g->r_refdef.yOrigin=.5f;_g->r_refdef.horizontalFieldOfView=2;
    _g->view_clipplanes[0].leftedge=true;_g->view_clipplanes[1].rightedge=true;_g->d_pzbuffer=d_zbuffer;R_InitTurb();
    vrect_t rect={0,0,320,200,NULL};R_ViewChanged(&rect,48,1);D_Init();return true;
}
bool mg24_draw(const qr_camera_t *camera,uint8_t*pixels){
    vid.buffer=pixels;vid.conbuffer=pixels;memcpy(_g->r_refdef.vieworg,camera->position,12);_g->r_refdef.viewangles[1]=camera->yaw;_g->cl.time+=1.0/60;
    R_SetupFrame();R_MarkLeaves();R_EdgeDrawing();return !r_outofedges && !r_outofsurfaces;
}

/* Fail loudly if game entities enter this deliberately world-only harness. */
vector get_qcc_origin(edict_t *e){(void)e;Sys_Error("Game entity origin unavailable in renderer harness");return (vector){{0}};}
vector get_qcc_angles(edict_t *e){(void)e;Sys_Error("Game entity angles unavailable in renderer harness");return (vector){{0}};}
float get_qcc_frame(edict_t *e){(void)e;Sys_Error("Game entity frame unavailable");return 0;}
float get_qcc_modelindex(edict_t *e){(void)e;Sys_Error("Game entity model unavailable");return 0;}
char *getStringFromIndex(int16_t index){(void)index;Sys_Error("Game strings unavailable");return NULL;}

#include "native_image.inc"
