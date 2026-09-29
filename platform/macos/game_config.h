#ifndef QMAC_GAME_CONFIG_H
#define QMAC_GAME_CONFIG_H
/* SDL must detect the real host before upstream's portable-C WIN32 selector. */
#include <SDL.h>
#include "mg24_config.h"
#define QMAC_GAME 1
extern unsigned char staticZone[];
#endif
