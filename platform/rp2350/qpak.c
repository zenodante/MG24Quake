#include "qpak.h"
#include <string.h>
#include <limits.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static bool range(uint32_t start, size_t count, uint32_t limit) {
    return start <= limit && count <= limit - start;
}
uint32_t qpak_crc32(const void *data, size_t length) {
    const uint8_t *p = data;
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *p++;
        for (unsigned bit=0; bit<8; ++bit)
            crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
static bool length_ext(const uint8_t **src, const uint8_t *end, size_t *n, size_t max) {
    unsigned b;
    do {
        if (*src == end) return false;
        b = *(*src)++;
        if (*n > max || b > max - *n) return false;
        *n += b;
    } while (b == 255);
    return true;
}
bool qpak_lz4_decode(const uint8_t *src, size_t size, uint8_t *dst, size_t expected) {
    const uint8_t *end = src + size;
    size_t pos = 0;
    while (src < end) {
        unsigned token = *src++;
        size_t literals = token >> 4;
        if (literals == 15 && !length_ext(&src,end,&literals,expected)) return false;
        if (literals > (size_t)(end-src) || literals > expected-pos) return false;
        memcpy(dst+pos,src,literals); src += literals; pos += literals;
        if (src == end) return pos == expected;
        if (end-src < 2) return false;
        unsigned distance = src[0] | (unsigned)src[1]<<8;
        src += 2;
        if (!distance || distance > pos) return false;
        size_t match = (token&15u) + 4;
        if ((token&15u)==15 && !length_ext(&src,end,&match,expected)) return false;
        if (match > expected-pos) return false;
        while (match--) { dst[pos] = dst[pos-distance]; ++pos; }
    }
    return pos == expected;
}
static bool qxip_open(qpak_t *pak,const uint8_t *p,size_t available) {
    /* <4s9I>: magic,version,files,str_off,dir_off,data_off,tex_off,tex_size,
       texture_count,image_bytes. Directory entries are <name_off,kind,off,size>. */
    if (available < 40 || memcmp(p,"QXIP",4) || rd32(p+4)!=1) return false;
    uint32_t files=rd32(p+8), strings=rd32(p+12), directory=rd32(p+16);
    uint32_t data=rd32(p+20), tex=rd32(p+24), tex_bytes=rd32(p+28);
    uint32_t texture_count=rd32(p+32), bytes=rd32(p+36);
    if (bytes<40 || bytes>available || bytes>QPAK_ASSET_CAPACITY || strings<40 ||
        directory<strings || files>(bytes-directory)/16 || !range(directory,(size_t)files*16,bytes) ||
        data<directory+(uint64_t)files*16 || data>bytes || !range(tex,tex_bytes,bytes)) return false;
    if (tex_bytes<12 || memcmp(p+tex,"TEX1",4) || rd32(p+tex+4)!=texture_count ||
        rd32(p+tex+8)!=12 || texture_count>(tex_bytes-12)/40) return false;
    for(uint32_t i=0;i<texture_count;++i){
        const uint8_t *te=p+tex+12+i*40;
        uint32_t off=rd32(te),size=rd32(te+4);
        if(off<12+texture_count*40 || !range(off,size,tex_bytes))return false;
    }
    for(uint32_t i=0;i<files;++i){
        const uint8_t *e=p+directory+i*16;
        uint32_t noff=rd32(e),kind=rd32(e+4),off=rd32(e+8),size=rd32(e+12);
        if(noff>=directory-strings || kind>1 || !range(off,size,bytes))return false;
        const uint8_t *name=p+strings+noff;
        if(!memchr(name,0,directory-(strings+noff)))return false;
    }
    *pak=(qpak_t){.image=p,.bytes=bytes,.files=files,.directory=directory,.payload=data,
        .strings=strings,.texture_store=tex,.texture_store_bytes=tex_bytes,
        .texture_count=texture_count,.format=QPAK_FORMAT_QXIP1};
    return true;
}
bool qpak_open(qpak_t *pak, const void *image, size_t available) {
    if (!pak || !image || available < 40) return false;
    memset(pak,0,sizeof(*pak));
    const uint8_t *p = image;
    if (!memcmp(p,"QXIP",4)) return qxip_open(pak,p,available);
    if (available < 64 || memcmp(p,"QRP2350\0",8) || rd32(p+8)!=1 || rd32(p+12)!=QPAK_BLOCK_BYTES) return false;
    qpak_t q = {.image=p, .bytes=rd32(p+16), .files=rd32(p+20), .blocks=rd32(p+24),
                .directory=rd32(p+28), .block_table=rd32(p+32), .payload=rd32(p+36),.format=QPAK_FORMAT_BLOCKS};
    if (q.bytes < 64 || q.bytes > available || q.bytes > QPAK_ASSET_CAPACITY || q.directory!=64 ||
        q.files > (q.bytes-64)/80 || q.block_table!=64+q.files*80 ||
        !range(q.block_table,0,q.bytes) || q.blocks>(q.bytes-q.block_table)/16 ||
        q.payload!=q.block_table+q.blocks*16 || !range(q.payload,0,q.bytes)) return false;
    if (qpak_crc32(p+64,q.payload-64)!=rd32(p+40)) return false;
    for (unsigned i=44;i<64;i+=4) if (rd32(p+i)) return false;
    uint32_t next_block=0, next_payload=q.payload;
    for (uint32_t i=0;i<q.files;++i) {
        const uint8_t *e=p+q.directory+i*80;
        uint32_t size=rd32(e+56), first=rd32(e+60), count=rd32(e+64);
        if (!e[0] || !memchr(e,0,56) || rd32(e+72) || rd32(e+76) ||
            first!=next_block || count!=size/QPAK_BLOCK_BYTES+(size%QPAK_BLOCK_BYTES!=0) ||
            !range(first,count,q.blocks)) return false;
        for (uint32_t j=0;j<count;++j) {
            const uint8_t *b=p+q.block_table+(first+j)*16;
            uint32_t off=rd32(b), stored=rd32(b+4), raw=rd32(b+8), flags=rd32(b+12);
            uint32_t expected=size-j*QPAK_BLOCK_BYTES;
            if (expected>QPAK_BLOCK_BYTES) expected=QPAK_BLOCK_BYTES;
            if (off!=next_payload || !stored || raw!=expected || flags>1 ||
                (flags ? stored>=raw : stored!=raw) || !range(off,stored,q.bytes)) return false;
            next_payload=(off+stored+3u)&~3u;
        }
        next_block+=count;
    }
    if (next_block!=q.blocks || next_payload!=q.bytes) return false;
    *pak=q;
    return true;
}
bool qpak_find(const qpak_t *pak, const char *name, qpak_file_t *file) {
    if (!pak || !pak->image || !name || !file) return false;
    if(pak->format==QPAK_FORMAT_QXIP1){
        for(uint32_t i=0;i<pak->files;++i){
            const uint8_t *e=pak->image+pak->directory+i*16;
            uint32_t noff=rd32(e);
            if(!strcmp((const char *)(pak->image+pak->strings+noff),name)){
                uint32_t kind=rd32(e+4),off=rd32(e+8),size=rd32(e+12);
                *file=(qpak_file_t){.size=size,.direct_offset=off,.kind=kind,.direct=true};
                return true;
            }
        }
        return false;
    }
    for (uint32_t i=0;i<pak->files;++i) {
        const uint8_t *e=pak->image+pak->directory+i*80;
        if (!strcmp((const char *)e,name)) {
            *file=(qpak_file_t){.size=rd32(e+56),.first_block=rd32(e+60),.block_count=rd32(e+64),.crc32=rd32(e+68)};
            return true;
        }
    }
    return false;
}
static bool file_range(const qpak_t *p, const qpak_file_t *f,uint32_t off,size_t len) {
    if(!p||!p->image||!f||!range(off,len,f->size))return false;
    if(f->direct)return range(f->direct_offset,f->size,p->bytes);
    return range(f->first_block,f->block_count,p->blocks) &&
           f->block_count==f->size/QPAK_BLOCK_BYTES+(f->size%QPAK_BLOCK_BYTES!=0);
}
const uint8_t *qpak_map(const qpak_t *pak,const qpak_file_t *f,uint32_t offset,size_t length) {
    if (!file_range(pak,f,offset,length) || !length) return NULL;
    if(f->direct)return pak->image+f->direct_offset+offset;
    uint32_t page=f->first_block+offset/QPAK_BLOCK_BYTES, within=offset%QPAK_BLOCK_BYTES;
    const uint8_t *result=NULL;
    size_t done=0;
    while (done<length) {
        const uint8_t *b=pak->image+pak->block_table+page++*16;
        if (rd32(b+12) || within>=rd32(b+8)) return NULL;
        const uint8_t *ptr=pak->image+rd32(b)+within;
        if (!result) result=ptr;
        else if (ptr!=result+done) return NULL;
        size_t n=rd32(b+8)-within;
        if (n>length-done) n=length-done;
        done+=n; within=0;
    }
    return result;
}
bool qpak_read(const qpak_t *pak,const qpak_file_t *f,qpak_cache_t *cache,
               uint32_t offset,void *output,size_t length) {
    if (!file_range(pak,f,offset,length) || (!output && length)) return false;
    if(f->direct){
        memcpy(output,pak->image+f->direct_offset+offset,length);
        return true;
    }
    uint8_t *out=output;
    while (length) {
        uint32_t page=f->first_block+offset/QPAK_BLOCK_BYTES, within=offset%QPAK_BLOCK_BYTES;
        const uint8_t *b=pak->image+pak->block_table+page*16;
        if (within>=rd32(b+8)) return false;
        const uint8_t *src=pak->image+rd32(b);
        if (rd32(b+12)) {
            if (!cache) return false;
            if (!cache->valid || cache->owner!=pak || cache->block!=page) {
                cache->valid=false;
                if (!qpak_lz4_decode(src,rd32(b+4),cache->data,rd32(b+8))) return false;
                cache->owner=pak; cache->block=page; cache->valid=true;
            }
            src=cache->data;
        }
        size_t n=rd32(b+8)-within;
        if (n>length) n=length;
        memcpy(out,src+within,n); out+=n; offset+=(uint32_t)n; length-=n;
    }
    return true;
}

bool qpak_level_open(const qpak_t *pak,const qpak_file_t *file,qpak_level_t *level) {
    if(!pak||!file||!level||pak->format!=QPAK_FORMAT_QXIP1||file->kind!=1||!file->direct||file->size<68)
        return false;
    const uint8_t *p=pak->image+file->direct_offset;
    if(memcmp(p,"LVL1",4))return false;
    uint32_t texture_count=rd32(p+4);
    uint32_t off[QXIP_BSP_LUMPS];
    for(unsigned i=0;i<QXIP_BSP_LUMPS;++i)off[i]=rd32(p+8+i*4);
    for(unsigned i=0;i<QXIP_BSP_LUMPS;++i){
        if(off[i]<68||off[i]>file->size||(i&&off[i]<off[i-1]))return false;
    }
    *level=(qpak_level_t){.base=p,.bytes=file->size,.texture_count=texture_count};
    for(unsigned i=0;i<QXIP_BSP_LUMPS;++i){
        level->lump_offset[i]=off[i];
        level->lump_size[i]=(i+1<QXIP_BSP_LUMPS?off[i+1]:file->size)-off[i];
    }
    if(level->lump_size[2] < 4u + (uint64_t)texture_count*4u || rd32(p+off[2])!=texture_count)
        return false;
    return true;
}
const uint8_t *qpak_level_lump(const qpak_level_t *level,unsigned lump,size_t *bytes) {
    if(!level||!level->base||lump>=QXIP_BSP_LUMPS)return NULL;
    if(bytes)*bytes=level->lump_size[lump];
    return level->base+level->lump_offset[lump];
}
bool qpak_level_texture_id(const qpak_level_t *level,unsigned index,uint32_t *texture_id) {
    if(!level||!texture_id||index>=level->texture_count)return false;
    const uint8_t *ids=level->base+level->lump_offset[2]+4;
    *texture_id=rd32(ids+index*4);
    return true;
}
const uint8_t *qpak_texture(const qpak_t *pak,uint32_t texture_id,size_t *bytes) {
    if(!pak||pak->format!=QPAK_FORMAT_QXIP1||texture_id>=pak->texture_count)return NULL;
    const uint8_t *store=pak->image+pak->texture_store;
    const uint8_t *entry=store+12+texture_id*40;
    uint32_t off=rd32(entry),size=rd32(entry+4);
    if(!range(off,size,pak->texture_store_bytes))return NULL;
    if(bytes)*bytes=size;
    return store+off;
}
