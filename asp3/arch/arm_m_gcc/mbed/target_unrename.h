/* This file is generated from target_rename.def by genrename. */

/* This file is included only when target_rename.h has been included. */
#ifdef TOPPERS_TARGET_RENAME_H
#undef TOPPERS_TARGET_RENAME_H


/*
 * target_config.c
 */
#undef target_initialize
#undef target_exit

#undef target_hrt_initialize
#undef target_hrt_terminate
#undef target_hrt_get_current
#undef target_hrt_raise_event
#undef target_hrt_set_event
#undef target_hrt_handler

#ifdef TOPPERS_LABEL_ASM


/*
 * target_config.c
 */
#undef _target_initialize
#undef _target_exit

#endif /* TOPPERS_LABEL_ASM */

#include "chip_unrename.h"

#endif /* TOPPERS_TARGET_RENAME_H */
