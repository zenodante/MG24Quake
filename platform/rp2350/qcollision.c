/* Swept hull traversal adapted from Quake's SV_RecursiveHullCheck in
 * QuakeMG24/Quake/world.c; iterative scratch storage for the RP2350 core stack.
 * Copyright (C) 1996-1997 Id Software, Inc.
 * Copyright (C) 2023-2024 Nicola Wrachien (MG24 port).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "qcollision.h"
#include <math.h>
#include <string.h>
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int32_t i32(const uint8_t *p){return (int32_t)u32(p);}
static int i16(const uint8_t *p){return (int16_t)(p[0]|(unsigned)p[1]<<8);}
static float f32(const uint8_t *p){uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f;}
static float dot(const float *a,const float *b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static bool vector_ok(const float *v){if(!v)return false;for(unsigned k=0;k<3;++k)if(!isfinite(v[k])||fabsf(v[k])>10000000)return false;return true;}
static float distance(const uint8_t *p,const float *v){unsigned type=p[16];return type<3?v[type]-f32(p+12):v[0]*f32(p)+v[1]*f32(p+4)+v[2]*f32(p+8)-f32(p+12);}
static bool terminal(const qbsp_t *b,unsigned hull,int32_t node,int32_t *value){
    if(!hull){const uint8_t *l=qbsp_record(b,QBSP_LEAVES,(uint32_t)(-(int64_t)node-1));if(!l)return false;node=i32(l);}
    if(node < -14 || node > -1)return false;
    *value=node;return true;
}
static bool point(const qbsp_t *b,unsigned hull,int32_t node,const float *p,int32_t *value){
    unsigned lump=hull?QBSP_CLIPNODES:QBSP_NODES;uint32_t budget=b->lump[lump].count;
    while(node>=0){if(!budget--)return false;const uint8_t *n=qbsp_record(b,lump,(uint32_t)node);if(!n)return false;
        const uint8_t *plane=qbsp_record(b,QBSP_PLANES,u32(n));if(!plane)return false;
        float d=distance(plane,p);if(!isfinite(d))return false;node=i16(n+4+(d<0?2:0));}
    return terminal(b,hull,node,value);
}
bool qc_trace(qc_workspace_t *w,const qbsp_t *b,uint32_t model,unsigned hull,
              const float *start,const float *end,qc_trace_t *out){
    if(!out)return false;
    *out=(qc_trace_t){.allsolid=true,.startsolid=true};
    if(vector_ok(start))memcpy(out->end,start,sizeof out->end);
    if(!w||!b||hull>3||!vector_ok(start)||!vector_ok(end))return false;
    const uint8_t *m=qbsp_record(b,QBSP_MODELS,model);if(!m)return false;
    int32_t root=i32(m+36+4*hull);unsigned lump=hull?QBSP_CLIPNODES:QBSP_NODES;
    uint32_t budget=b->lump[lump].count*8+128;
    qc_trace_t t={.fraction=1,.allsolid=true};memcpy(t.end,end,sizeof t.end);
    unsigned count=1;w->frames[0]=(qc_frame_t){.node=root,.af=0,.bf=1};
    memcpy(w->frames[0].a,start,12);memcpy(w->frames[0].b,end,12);
    while(count){if(!budget--)return false;qc_frame_t *f=&w->frames[count-1];
        if(!f->state){
            if(f->node<0){int32_t value;if(!terminal(b,hull,f->node,&value))return false;
                if(value==-2)t.startsolid=true;else{t.allsolid=false;if(value==-1)t.inopen=true;else t.inwater=true;}
                --count;continue;}
            const uint8_t *n=qbsp_record(b,lump,(uint32_t)f->node);if(!n)return false;
            f->plane=u32(n);const uint8_t *p=qbsp_record(b,QBSP_PLANES,f->plane);if(!p)return false;
            float a=distance(p,f->a),z=distance(p,f->b);if(!isfinite(a)||!isfinite(z))return false;
            if(a>=0 && z>=0){f->node=i16(n+4);continue;}
            if(a<0 && z<0){f->node=i16(n+6);continue;}
            f->side=a<0;f->frac=fminf(1,fmaxf(0,(a+(a<0?0.03125f:-0.03125f))/(a-z)));
            for(unsigned k=0;k<3;++k)f->mid[k]=f->a[k]+f->frac*(f->b[k]-f->a[k]);
            f->far=i16(n+4+2*(f->side^1u));f->state=1;
            if(count==QC_TRACE_STACK)return false;
            qc_frame_t *child=&w->frames[count++];*child=(qc_frame_t){.node=i16(n+4+2*f->side),.af=f->af,.bf=f->af+(f->bf-f->af)*f->frac};
            memcpy(child->a,f->a,12);memcpy(child->b,f->mid,12);continue;
        }
        int32_t value;if(!point(b,hull,f->far,f->mid,&value))return false;
        if(value!=-2){f->node=f->far;f->af=f->af+(f->bf-f->af)*f->frac;memcpy(f->a,f->mid,12);f->state=0;continue;}
        if(t.allsolid)break;
        const uint8_t *p=qbsp_record(b,QBSP_PLANES,f->plane);float sign=f->side?-1:1;
        for(unsigned k=0;k<3;++k)t.normal[k]=sign*f32(p+4*k);
        t.distance=sign*f32(p+12);
        // Original Quake's numerical backup, bounded to at most eleven steps.
        for(unsigned attempt=0;attempt<12;++attempt){
            if(!point(b,hull,root,f->mid,&value))return false;
            if(value!=-2)break;
            f->frac-=0.1f;if(f->frac<0)return false; // fail closed rather than return an embedded endpoint
            for(unsigned k=0;k<3;++k)f->mid[k]=f->a[k]+f->frac*(f->b[k]-f->a[k]);
        }
        t.fraction=f->af+(f->bf-f->af)*f->frac;memcpy(t.end,f->mid,12);break;
    }
    *out=t;return true;
}
bool qc_slide(qc_workspace_t *w,const qbsp_t *b,float *position,const float *displacement){
    if(!vector_ok(position)||!vector_ok(displacement))return false;
    float pos[3],move[3],planes[4][3];unsigned count=0;
    memcpy(pos,position,12);memcpy(move,displacement,12);
    for(unsigned bump=0;bump<4;++bump){float end[3];for(unsigned k=0;k<3;++k)end[k]=pos[k]+move[k];
        qc_trace_t t;if(!qc_trace(w,b,0,1,pos,end,&t)||t.startsolid||t.allsolid)return false;
        memcpy(pos,t.end,12);if(t.fraction==1)break;
        memcpy(planes[count++],t.normal,12);
        for(unsigned k=0;k<3;++k)move[k]*=1-t.fraction;
        float candidate[3];bool found=false;
        for(unsigned i=0;i<count;++i){float into=dot(move,planes[i]);
            for(unsigned k=0;k<3;++k)candidate[k]=move[k]-into*planes[i][k];
            bool valid=true;for(unsigned j=0;j<count;++j)if(j!=i && dot(candidate,planes[j]) < -0.0001f)valid=false;
            if(valid){found=true;break;}}
        if(!found && count==2){float line[3]={planes[0][1]*planes[1][2]-planes[0][2]*planes[1][1],
            planes[0][2]*planes[1][0]-planes[0][0]*planes[1][2],planes[0][0]*planes[1][1]-planes[0][1]*planes[1][0]};
            float length=dot(line,line);if(length>0.000001f){float scale=dot(move,line)/length;for(unsigned k=0;k<3;++k)candidate[k]=line[k]*scale;found=true;}}
        if(!found || dot(candidate,displacement)<=0)break;
        memcpy(move,candidate,12);
    }
    memcpy(position,pos,12);return true;
}
