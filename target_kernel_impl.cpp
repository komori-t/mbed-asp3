#include "mbed.h"

extern "C" {
    #include "kernel_rename.h"
    extern void core_initialize(void);
    extern void core_terminate(void);
    void target_initialize(void);
    void target_exit(void);
    void software_term_hook(void);
}

/*
 * ターゲット依存部 初期化処理
 */
void target_initialize(void)
{
    /*
     *  コア依存部の初期化
     */
    core_initialize();
}

/*
 * ターゲット依存部 終了処理
 */
void target_exit(void)
{
    /* チップ依存部の終了処理 */
    core_terminate();
    while (1) __asm__ volatile ("wfi");
}

/*
 * エラー発生時の処理
 */
void Error_Handler(void)
{
    mbed_die();
}

void software_term_hook(void) {}
