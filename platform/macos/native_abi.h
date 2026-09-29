/* Shared offline compiler/player structure fingerprint. */
static uint32_t native_abi(void){
    const uint32_t layout[]={2, sizeof(dclipnode_t), sizeof(void*),sizeof(model_t),sizeof(brush_model_data_t),
        sizeof(mplane_t),sizeof(mtexinfo_t),sizeof(msurface_t),sizeof(mnode_t),sizeof(mleaf_t),sizeof(texture_t),
        offsetof(msurface_t,samples),offsetof(msurface_t,texinfo),offsetof(msurface_t,plane),
        offsetof(mnode_t,plane),offsetof(texture_t,anim_next),offsetof(texture_t,extmemdata),
        offsetof(brush_model_data_t,planes),offsetof(brush_model_data_t,textures)};
    return qpak_crc32(layout,sizeof layout);
}
