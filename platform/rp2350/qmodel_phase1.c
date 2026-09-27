/*
 * Phase-1 adapters for the real MG24 model loader.
 *
 * These are deliberately narrow: they preserve model.c's ABI while avoiding
 * pulling the complete QuakeC/entity and renderer dependency graphs into the
 * model-loader hardware probe.  They are not production renderer/game stubs:
 * the production targets will use the original MG24 string tables and sky
 * renderer when those subsystems are integrated.
 */
#include "quakedef.h"
#include "model.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * model_t stores only a 16-bit name index in the minimized MG24 build.  The
 * original implementation resolves that index through the generated QuakeC,
 * entity, submodel and pak string tables in pr_edict.c.  Phase 1 needs only
 * stable model names, so keep a small intern table with the same public ABI.
 * This avoids linking the entire entity/QuakeC runtime merely to load a BSP.
 */
#define PHASE1_MODEL_STRINGS 192
#define PHASE1_MODEL_NAME_MAX 64

static char phase1_model_strings[PHASE1_MODEL_STRINGS][PHASE1_MODEL_NAME_MAX];
static uint16_t phase1_model_string_count;

int16_t findStringIndex(const char *value)
{
    uint16_t i;

    if (!value || !value[0])
        return 0;

    for (i = 0; i < phase1_model_string_count; ++i) {
        if (!strcmp(phase1_model_strings[i], value))
            return (int16_t)(i + 1u);
    }

    if (phase1_model_string_count >= PHASE1_MODEL_STRINGS) {
        printf("phase1 model string table full: %s\n", value);
        return INT16_MIN;
    }

    size_t len = strlen(value);
    if (len >= PHASE1_MODEL_NAME_MAX) {
        printf("phase1 model name too long: %s\n", value);
        return INT16_MIN;
    }

    memcpy(phase1_model_strings[phase1_model_string_count], value, len + 1u);
    ++phase1_model_string_count;
    return (int16_t)phase1_model_string_count;
}

char *getStringFromIndex(int16_t index)
{
    if (index <= 0 || (uint16_t)index > phase1_model_string_count)
        return NULL;
    return phase1_model_strings[(uint16_t)index - 1u];
}

/*
 * MG24's missing-texture object normally comes from R_InitTextures().  The
 * loader only needs a valid texture_t pointer when a texinfo references a
 * missing miptex, so provide the same object shape here.  Pixel data is a
 * small checker pattern resident in SRAM; this path is diagnostic only.
 */
static uint8_t phase1_notexture_mip0[16u * 16u];
static uint8_t phase1_notexture_mip1[8u * 8u];
static uint8_t phase1_notexture_mip2[4u * 4u];
static uint8_t phase1_notexture_mip3[2u * 2u];
static texture_t phase1_notexture;
texture_t *r_notexture_mip = &phase1_notexture;

static void fill_checker(uint8_t *dst, unsigned width, unsigned height)
{
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x)
            dst[y * width + x] = ((x ^ y) & 4u) ? 0u : 255u;
    }
}

void qmodel_phase1_init(void)
{
    memset(&phase1_notexture, 0, sizeof(phase1_notexture));
    phase1_notexture.width = 16;
    phase1_notexture.height = 16;
    phase1_notexture.extmemdata[0] = phase1_notexture_mip0;
    phase1_notexture.extmemdata[1] = phase1_notexture_mip1;
    phase1_notexture.extmemdata[2] = phase1_notexture_mip2;
    phase1_notexture.extmemdata[3] = phase1_notexture_mip3;
    fill_checker(phase1_notexture_mip0, 16, 16);
    fill_checker(phase1_notexture_mip1, 8, 8);
    fill_checker(phase1_notexture_mip2, 4, 4);
    fill_checker(phase1_notexture_mip3, 2, 2);
}

/*
 * On MG24 R_InitFlashSky() switches to separately converted skyXX resources.
 * RP2350 QXIP keeps the BSP miptex bytes directly addressable, and model.c has
 * already populated texture_t.extmemdata[] with those XIP addresses before
 * this callback.  For the Phase-1 loader there is therefore no storage work
 * to perform here.  Phase 2 will attach the original MG24 sky animation logic
 * to these direct-XIP source pixels.
 */
void R_InitFlashSky(miptex_t *mt, char *modelName)
{
    if (!mt)
        Sys_Error("R_InitFlashSky: null miptex");
    (void)modelName;
}
