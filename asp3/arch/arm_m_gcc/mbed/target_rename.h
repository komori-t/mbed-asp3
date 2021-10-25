/* This file is generated from target_rename.def by genrename. */

#ifndef TOPPERS_TARGET_RENAME_H
#define TOPPERS_TARGET_RENAME_H


/*
 * target_config.c
 */
#define target_initialize			_kernel_target_initialize
#define target_exit					_kernel_target_exit

#define target_hrt_initialize  _kernel_target_hrt_initialize
#define target_hrt_terminate   _kernel_target_hrt_terminate
#define target_hrt_get_current _kernel_target_hrt_get_current
#define target_hrt_raise_event _kernel_target_hrt_raise_event
#define target_hrt_set_event   _kernel_target_hrt_set_event
#define target_hrt_handler     _kernel_target_hrt_handler

#ifdef TOPPERS_LABEL_ASM


/*
 * target_config.c
 */
#define _target_initialize			__kernel_target_initialize
#define _target_exit				__kernel_target_exit

#endif /* TOPPERS_LABEL_ASM */

#include "chip_rename.h"

#endif /* TOPPERS_TARGET_RENAME_H */
