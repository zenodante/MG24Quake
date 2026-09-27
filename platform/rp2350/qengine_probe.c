/* Phase-1 compile probe for the original MG24 Quake engine headers.
 *
 * This deliberately does not implement a second renderer or engine.  It makes
 * the RP2350 target compile against the real Quake/MG24 public data model so
 * incompatible platform assumptions are discovered while the proven core-1
 * service remains untouched.
 */
#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"
#include "sys.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(uint8_t) == 1, "Quake assets require 8-bit bytes");
_Static_assert(sizeof(uint16_t) == 2, "Quake assets require 16-bit words");
_Static_assert(sizeof(uint32_t) == 4, "Quake assets require 32-bit words");
_Static_assert(sizeof(float) == 4, "MG24 renderer assumes IEEE-754 binary32");

/* Kept non-static so the linker/map file exposes the engine ABI checkpoint. */
size_t qengine_phase1_abi_probe(void)
{
    return sizeof(model_t) + sizeof(msurface_t) + sizeof(mplane_t) +
           sizeof(medge_t) + sizeof(entity_t) + sizeof(espan_t);
}
