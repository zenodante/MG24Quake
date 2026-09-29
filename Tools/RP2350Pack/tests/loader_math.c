/* Host-only independent transcription of model.c's Length/CalcSurfaceExtents.
 * No MCU headers, serialized runtime records, or Python compiler helpers. */
#include <math.h>
int reference_mip(const float *v) {
    float a = sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    float b = sqrtf(v[4]*v[4]+v[5]*v[5]+v[6]*v[6]);
    a = (a+b)/2;
    return a < 0.32 ? 4 : a < 0.49 ? 3 : a < 0.99 ? 2 : 1;
}
void reference_extents(const float *vertices, int count, const float *vecs, int *out) {
    for (int axis=0; axis<2; ++axis) {
        float lo=INFINITY, hi=-INFINITY;
        const float *t=vecs+axis*4;
        for (int i=0; i<count; ++i) {
            const float *v=vertices+i*3;
            float value=v[0]*t[0]+v[1]*t[1]+v[2]*t[2]+t[3];
            if (value<lo) lo=value;
            if (value>hi) hi=value;
        }
        int a=(int)floorf(lo/16), b=(int)ceilf(hi/16);
        out[axis]=a*16; out[axis+2]=(b-a)*16;
    }
}
