#ifndef AUTOCAL_BOARD_H
#define AUTOCAL_BOARD_H

#include "main.h"
#include "max35103.h"
#include "max35103_autocal.h"

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
void AUTOCAL_Start(Max35103Driver *driver,
                   const Max35103Profile *seed_profile);
void AUTOCAL_Poll(void);
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#endif /* AUTOCAL_BOARD_H */