#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"
uint8_t textureCacheBuffer[MAX_TEXTURE_SIZE];
byte *r_skysource;
static byte *texture_source;
extern uint8_t *nodeHadDlight;
void R_InitFlashSky(miptex_t *mt,char *name){(void)mt;(void)name;}
float skyspeed=8,skytime;
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
    if(r_fullbright || !_g->cl.worldmodel->brushModelData->lightdata){for(int i=0;i<n;i++)_g->blocklights[i]=0;}
    else if(s->samples){const byte*p=s->samples;for(int m=0;m<4 && s->styles[m]!=255;m++,p+=n)for(int i=0;i<n;i++)_g->blocklights[i]+=p[i]*r_drawsurf.lightadj[m];}
    if(s->surfNodeIndex>=0 && nodeHadDlight && (nodeHadDlight[s->surfNodeIndex/8] & (1u<<(s->surfNodeIndex%8)))) R_AddDynamicLights();
    R_DrawSurface();*width=r_drawsurf.surfwidth;return texture_source;
}
