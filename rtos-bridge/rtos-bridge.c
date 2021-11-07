#include "cmsis.h"
#include "kernel.h"
#define RTX_CORE_CM_H_ /* rtx_core_cm.h と bool_t が衝突する */
#include "cmsis_os2.h"
#include "rtx_lib.h"
#include "t_syslog.h"
#include "kernel_cfg.h"
#include "sil.h"
#include "rtos-bridge.h"
#include "rtos-config.h"

#define THREAD_WAIT_FLAGS_NONE     0
#define THREAD_WAIT_FLAGS_AND      1
#define THREAD_WAIT_FLAGS_OR       2

static TASK mbed_main_thread_entry;
static PRI mbed_main_thread_pri;
static STK_T *mbed_main_thread_stack;
static size_t mbed_main_thread_stack_size;

void __rtos_bridge_proxy_thread(intptr_t exinf)
{
    T_CTSK taskInfo;
    taskInfo.tskatr = TA_ACT;
    taskInfo.exinf = 0;
    taskInfo.task = mbed_main_thread_entry;
    taskInfo.itskpri = mbed_main_thread_pri;
    taskInfo.stksz = mbed_main_thread_stack_size;
    taskInfo.stk = mbed_main_thread_stack;
    acre_tsk(&taskInfo);

    intptr_t detachedThreadID;
    while (1) {
        assert(rcv_dtq(__RTOS_BRIDGE_PROXY_QUEUE, &detachedThreadID) == E_OK);
        del_tsk((ID)detachedThreadID);
    }
}

void _kernel_target_hrt_initialize(intptr_t exinf);
void _kernel_set_hrt_event(void);

void mbed_main(void)
{
    /*
     * この関数は Mbed のランタイムによって main() 関数の直前に呼び出され、タイマの初期化を行う。
     * sta_ker() した（osKernelStart が呼び出された）時点ではまだ Timer オブジェクトが
     * 初期化されていないため、タイマドライバを呼び出せない。
     */
    _kernel_target_hrt_initialize(0);
    loc_cpu();
    _kernel_set_hrt_event();
    unl_cpu();
}

//  OS Runtime Information
osRtxInfo_t osRtxInfo __attribute__((section(".data.os"))) =
//lint -e{785} "Initialize only OS ID, OS Version and Kernel State"
{ .os_id = osRtxKernelId, .version = osRtxVersionKernel, .kernel.state = osRtxKernelInactive };

/// Initialize the RTOS Kernel.
/// \return status code that indicates the execution status of the function.
osStatus_t osKernelInitialize (void)
{
    osRtxInfo.kernel.state = osKernelReady;
    return osOK;
}

///  Get RTOS Kernel Information.
/// \param[out]    version       pointer to buffer for retrieving version information.
/// \param[out]    id_buf        pointer to buffer for retrieving kernel identification string.
/// \param[in]     id_size       size of buffer for kernel identification string.
/// \return status code that indicates the execution status of the function.
osStatus_t osKernelGetInfo (osVersion_t *version, char *id_buf, uint32_t id_size)
{
    uint32_t size;

    if (version != NULL) {
        version->api    = osRtxVersionAPI;
        version->kernel = osRtxVersionKernel;
    }

    if ((id_buf != NULL) && (id_size != 0U)) {
        if (id_size > sizeof(osRtxKernelId)) {
            size = sizeof(osRtxKernelId);
        } else {
            size = id_size;
        }
        (void)memcpy(id_buf, osRtxKernelId, size);
    }

    return osOK;
}

/// Get the current RTOS Kernel state.
/// \return current RTOS Kernel state.
osKernelState_t osKernelGetState (void)
{
    return osRtxInfo.kernel.state;
}

void sta_ker(void);

/// Start the RTOS Kernel scheduler.
/// \return status code that indicates the execution status of the function.
osStatus_t osKernelStart (void)
{
    osRtxInfo.kernel.state = osKernelRunning;
    Asm("cpsid f");
    sta_ker();
    return osError;
}

/// Lock the RTOS Kernel scheduler.
/// \return previous lock state (1 - locked, 0 - not locked, error code if negative).
int32_t osKernelLock (void)
{
    const int32_t locked = sns_dsp();
    dis_dsp();
    return locked;
}

/// Unlock the RTOS Kernel scheduler.
/// \return previous lock state (1 - locked, 0 - not locked, error code if negative).
int32_t osKernelUnlock (void)
{
    const int32_t locked = sns_dsp();
    ena_dsp();
    return locked;
}

/// Restore the RTOS Kernel scheduler lock state.
/// \param[in]     lock          lock state obtained by \ref osKernelLock or \ref osKernelUnlock.
/// \return new lock state (1 - locked, 0 - not locked, error code if negative).
int32_t osKernelRestoreLock (int32_t lock)
{
    if (lock) {
        dis_dsp();
        return 1;
    } else {
        ena_dsp();
        return 0;
    }
}

/// Suspend the RTOS Kernel scheduler.
/// \return time in ticks, for how long the system can sleep or power-down.
uint32_t osKernelSuspend (void)
{
    /*
     * osKernelSuspend() と osKernelResume() はアイドルタスクから呼び出し、
     * カーネルのタイムティックを無効にするために用いられる。ASP3 カーネルは
     * もとよりティックレスであるから実装する必要はない。
     */
    return 0;
}

/// Resume the RTOS Kernel scheduler.
/// \param[in]     sleep_ticks   time in ticks for how long the system was in sleep or power-down mode.
void osKernelResume (uint32_t sleep_ticks)
{
}

/// Get the RTOS kernel tick count.
/// \return RTOS kernel current tick count.
uint32_t osKernelGetTickCount (void)
{
    /*
     * CMSIS-RTOS のティック周期は通常 1kHz であるが、ASP3 は 1MHz である。
     * そのため 1000 で除算しているが、アプリケーションからみると予期しないタイミング
     * でタイムティックがオーバーフローすることになる。get_tim() を使えば 64bit
     * のシステム時刻が得られるが、osKernelGetTickCount() が ISR から呼び出せる
     * ことに対し、get_tim() は非タスクコンテキストから呼び出すことができない。
     * あまり必要性が大きい API ではないと思われるため、この実装で妥協している。
     * 厳密な値が必要なら target_timer.cpp のタイマオブジェクトから64ビット値を読み取れば良い。
     */
    return fch_hrt() / 1000;
}

/// Get the RTOS kernel tick frequency.
/// \return frequency of the kernel tick in hertz, i.e. kernel ticks per second.
uint32_t osKernelGetTickFreq (void)
{
    return 1000;
}

/// Get the RTOS kernel system timer count.
/// \return RTOS kernel current system timer count as 32-bit value.
uint32_t osKernelGetSysTimerCount (void)
{
    return fch_hrt();
}

/// Get the RTOS kernel system timer frequency.
/// \return frequency of the system timer in hertz, i.e. timer ticks per second.
uint32_t osKernelGetSysTimerFreq (void)
{
    return 1000000;
}

#define THREAD_MEMB(id, memb) (((osRtxThread_t *)id)->memb)

#define THREAD_ATTR_NOT_DETACHED 0
#define THREAD_ATTR_DETACHED     1

static void task_start_hook(intptr_t exinf)
{
    osRtxThread_t *thread = (osRtxThread_t *)exinf;
    ((void (*)(void *))thread->thread_addr)((void *)thread->delay);
    osThreadExit();
}

/* 最高優先度はプロキシスレッド専用 */
#define PRIORITY_RTX_2_ASP(prio) (((TMIN_TPRI+1) - TMAX_TPRI) * ((prio) - osPriorityLow) / (osPriorityRealtime7 - osPriorityLow) + TMAX_TPRI)
#define PRIORITY_ASP_2_RTX(prio) ((osPriorityRealtime7 - osPriorityLow) * ((prio) - TMAX_TPRI) / ((TMIN_TPRI+1) - TMAX_TPRI) + osPriorityLow)

/// Create a thread and add it to Active Threads.
/// \param[in]     func          thread function.
/// \param[in]     argument      pointer that is passed to the thread function as start argument.
/// \param[in]     attr          thread attributes; NULL: default values.
/// \return thread ID for reference by other functions or NULL in case of error.
osThreadId_t osThreadNew (osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
{
    /*
     * 本当は関数全体を CPU ロック状態で動作させたいが、acre_tsk() が呼び出せなくなる。
     * まともなアプリケーションであればこの関数がスレッド ID をリターンする前にスレッド
     * ID を決め打ちで API 呼び出ししたりしないはず。
     */
    if (attr == NULL || attr->cb_mem == NULL) {
        /* とりあえず動的メモリ確保は未サポート */
        return NULL;
    }
    if (osRtxInfo.kernel.state != osKernelRunning) {
        /*
         * Mbed はカーネル起動前にメインスレッド作成のためにこの関数を呼び出す。
         * メインスレッドは代わりにプロキシで作成することとし、ここではリターンする。
         * NULL を返すとエラーとみなされてハングしてしまうので、ダミー値を返す。
         */
        mbed_main_thread_entry = (TASK)(void *)func;
        mbed_main_thread_pri = PRIORITY_RTX_2_ASP(attr->priority);
        mbed_main_thread_stack = (STK_T *)attr->stack_mem;
        mbed_main_thread_stack_size = attr->stack_size / COUNT_STK_T(1);
        return (osThreadId_t)1;
    }
    osRtxThread_t *thread = (osRtxThread_t *)attr->cb_mem;
    T_CTSK taskInfo;
    taskInfo.tskatr = 0;
    taskInfo.exinf = (intptr_t)thread;
    taskInfo.task = task_start_hook;
    taskInfo.itskpri = PRIORITY_RTX_2_ASP(attr->priority);
    taskInfo.stksz = attr->stack_size / COUNT_STK_T(1);
    taskInfo.stk = (STK_T *)attr->stack_mem;
    ER_ID id = acre_tsk(&taskInfo);
    if (id < 0) {
        return NULL;
    }
    thread->id = id;
    thread->attr = THREAD_ATTR_NOT_DETACHED;
    thread->name = attr->name;
    thread->thread_next = NULL;
    thread->thread_join = NULL;
    thread->stack_mem = attr->stack_mem;
    thread->stack_size = attr->stack_size;
    thread->flags_options = THREAD_WAIT_FLAGS_NONE;
    thread->wait_flags = 0;
    thread->thread_flags = 0;
    thread->thread_addr = (uint32_t)func;
    thread->delay = (uint32_t)argument; /* とりあえず空いているメンバに入れておく */
    act_tsk(thread->id);
    return (osThreadId_t)thread;
}

/// Get name of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return name as null-terminated string.
const char *osThreadGetName (osThreadId_t thread_id)
{
    return THREAD_MEMB(thread_id, name);
}

/// Return the thread ID of the current running thread.
/// \return thread ID for reference by other functions or NULL in case of error.
osThreadId_t osThreadGetId (void)
{
    intptr_t exinf;
    if (get_inf(&exinf) == E_OK) {
        return (osThreadId_t)exinf;
    }
    return NULL;
}

/// Get current thread state of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return current thread state of the specified thread.
osThreadState_t osThreadGetState (osThreadId_t thread_id)
{
    STAT stat;
    const ER ercd = get_tst(THREAD_MEMB(thread_id, id), &stat);
    if (ercd != E_OK) {
        return osThreadError;
    }
    switch (stat) {
        case TTS_RUN: return osThreadRunning;
        case TTS_RDY: return osThreadReady;
        case TTS_WAI: return osThreadBlocked;
        case TTS_SUS: return osThreadBlocked;
        case TTS_WAS: return osThreadBlocked;
        case TTS_DMT: return osThreadTerminated; /* Inactive? Terminated? */
        default: return osThreadError;
    }
}

/// Get stack size of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return stack size in bytes.
uint32_t osThreadGetStackSize (osThreadId_t thread_id)
{
    return THREAD_MEMB(thread_id, stack_size);
}

/// Get available stack space of a thread based on stack watermark recording during execution.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return remaining stack space in bytes.
uint32_t osThreadGetStackSpace (osThreadId_t thread_id)
{
    /* 本当は TCB からスタックポインタを参照しなければならない */
    return 0;
}

/// Change priority of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \param[in]     priority      new priority value for the thread function.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadSetPriority (osThreadId_t thread_id, osPriority_t priority)
{
    switch (chg_pri(THREAD_MEMB(thread_id, id), PRIORITY_RTX_2_ASP(priority))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_NOSPT: return osErrorResource;
        case E_ID:    return osErrorParameter;
        case E_PAR:   return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_ILUSE: return osErrorResource;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
}

/// Get current priority of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return current priority value of the specified thread.
osPriority_t osThreadGetPriority (osThreadId_t thread_id)
{
    PRI pri;
    switch (get_pri(THREAD_MEMB(thread_id, id), &pri)) {
        case E_OK: return PRIORITY_ASP_2_RTX(pri);
        default: return osPriorityError;
    }
}

/// Pass control to next thread that is in state \b READY.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadYield (void)
{
    switch (rot_rdq(TPRI_SELF)) {
        case E_OK:  return osOK;
        case E_CTX: return osErrorISR;
        default: return osError;
    }
}

/// Suspend execution of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadSuspend (osThreadId_t thread_id)
{
    switch (sus_tsk(THREAD_MEMB(thread_id, id))) {
        case E_OK:     return osOK;
        case E_CTX:    return osErrorISR;
        case E_NOSPT:  return osErrorResource;
        case E_ID:     return osErrorParameter;
        case E_NOEXS:  return osErrorParameter;
        case E_OBJ:    return osErrorResource;
        case E_RASTER: return osErrorResource;
        case E_QOVR:   return osErrorResource;
        default: return osError;
    }
    return osOK;
}

/// Resume execution of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadResume (osThreadId_t thread_id)
{
    switch (rsm_tsk(THREAD_MEMB(thread_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_NOSPT: return osErrorResource;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
    return osOK;
}

/// Detach a thread (thread storage can be reclaimed when thread terminates).
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadDetach (osThreadId_t thread_id)
{
    chg_ipm(TMIN_INTPRI);
    STAT stat;
    const ER ercd = get_tst(THREAD_MEMB(thread_id, id), &stat);
    osStatus_t status;
    switch (ercd) {
        case E_OK:
            if (stat == TTS_DMT) {
                del_tsk(THREAD_MEMB(thread_id, id));
            } else {
                THREAD_MEMB(thread_id, attr) = THREAD_ATTR_DETACHED;
            }
            status = osOK;
            break;
        case E_CTX:   status = osErrorISR; break;
        case E_ID:    status = osErrorParameter; break;
        case E_NOEXS: status = osErrorParameter; break;
        default: status = osError; break;
    }
    chg_ipm(TIPM_ENAALL);
    return status;
}

/// Wait for specified thread to terminate.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadJoin (osThreadId_t thread_id)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    chg_ipm(TMIN_INTPRI);
    if (THREAD_MEMB(thread_id, attr) == THREAD_ATTR_DETACHED) {
        chg_ipm(TIPM_ENAALL);
        return osErrorResource;
    }
    STAT stat;
    const ER ercd = get_tst(THREAD_MEMB(thread_id, id), &stat);
    osStatus_t status;
    switch (ercd) {
        case E_OK:
            switch (stat) {
                case TTS_RUN:
                    /* 自分自身には join 不可 */
                    status = osErrorResource;
                    break;
                case TTS_DMT:
                    status = osOK;
                    break;
                default:
                {
                    osRtxThread_t *thisThread = (osRtxThread_t *)osThreadGetId();
                    if (THREAD_MEMB(thread_id, thread_join) == NULL) {
                        THREAD_MEMB(thread_id, thread_join) = thisThread;
                    } else {
                        osRtxThread_t *last;
                        for (last = (osRtxThread_t *)thread_id;
                             THREAD_MEMB(last, thread_next) != NULL;
                             last = THREAD_MEMB(last, thread_next)) ;
                        THREAD_MEMB(last, thread_next) = thisThread;
                    }
                    chg_ipm(TIPM_ENAALL);
                    /*
                     * ここで割り込みが発生して join 対象のスレッドが終了しても、
                     * 起床要求はキューイングされるため問題なく slp_tsk() から返る。
                     */
                    /*
                     * ビットフラグ待ちにも slp_tsk() を使うが、ここで起床待ちになった
                     * 場合は osThreadFlagsSet() によって起こされることはないので干渉しない。
                     */
                    slp_tsk();
                }
                    return osOK;
            }
            break;
        case E_CTX:   status = osErrorISR; break;
        case E_ID:    status = osErrorParameter; break;
        case E_NOEXS: status = osErrorParameter; break;
        default: status = osError; break;
    }
    chg_ipm(TIPM_ENAALL);
    return status;
}

/// Terminate execution of current running thread.
void osThreadExit (void)
{
    const osRtxThread_t *thread = (osRtxThread_t *)osThreadGetId();
    if (thread) {
        chg_ipm(TMIN_INTPRI);
        osRtxThread_t *joinedThread = THREAD_MEMB(thread, thread_join);
        if (joinedThread) {
            wup_tsk(THREAD_MEMB(joinedThread, id));
            for (joinedThread = THREAD_MEMB(thread, thread_next);
                 joinedThread != NULL;
                 joinedThread = THREAD_MEMB(joinedThread, thread_next)) {
                wup_tsk(THREAD_MEMB(joinedThread, id));
            }
        }
        if (THREAD_MEMB(thread, attr) == THREAD_ATTR_DETACHED) {
            assert(psnd_dtq(__RTOS_BRIDGE_PROXY_QUEUE, (intptr_t)THREAD_MEMB(thread, id)) == E_OK);
        }
        ext_tsk(); /* ext_tsk() は割り込み優先度マスクを全解除にしてくれる */
    }
    while (1) ;
}

/// Terminate execution of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \return status code that indicates the execution status of the function.
osStatus_t osThreadTerminate (osThreadId_t thread_id)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    osThreadId_t thisThread;
    switch (get_inf((intptr_t *)&thisThread)) {
        case E_OK:  break;
        case E_CTX: return osErrorISR;
        default: return osError;
    }
    if ((osRtxThread_t *)thread_id == thisThread) {
        osThreadExit();
    }
    chg_ipm(TMIN_INTPRI);
    switch (ter_tsk(THREAD_MEMB(thread_id, id))) {
        case E_OK:    break;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_ILUSE: return osErrorResource;
        case E_OBJ:   return osErrorResource;
    }
    osRtxThread_t *joinedThread = THREAD_MEMB(thread_id, thread_join);
    if (joinedThread) {
        wup_tsk(THREAD_MEMB(joinedThread, id));
        for (joinedThread = THREAD_MEMB(thread_id, thread_next);
            joinedThread != NULL;
            joinedThread = THREAD_MEMB(joinedThread, thread_next)) {
            wup_tsk(THREAD_MEMB(joinedThread, id));
        }
    }
    if (THREAD_MEMB(thread_id, attr) == THREAD_ATTR_DETACHED) {
        del_tsk(THREAD_MEMB(thread_id, id));
    }
    chg_ipm(TIPM_ENAALL);
    return osOK;
}

/// Get number of active threads.
/// \return number of active threads.
uint32_t osThreadGetCount (void)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    /* CMSIS-RTOS 互換のスレッドのみを数えると複雑になるので省略する。 */
    return 0;
}

/// Enumerate active threads.
/// \param[out]    thread_array  pointer to array for retrieving thread IDs.
/// \param[in]     array_items   maximum number of items in array for retrieving thread IDs.
/// \return number of enumerated threads.
uint32_t osThreadEnumerate (osThreadId_t *thread_array, uint32_t array_items)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    /* 実装が複雑になるので省略する。 */
    return 0;
}

/// Set the specified Thread Flags of a thread.
/// \param[in]     thread_id     thread ID obtained by \ref osThreadNew or \ref osThreadGetId.
/// \param[in]     flags         specifies the flags of the thread that shall be set.
/// \return thread flags after setting or error code if highest bit set.
uint32_t osThreadFlagsSet (osThreadId_t thread_id, uint32_t flags)
{
    const int isr = sns_ctx();
    if (isr) {
        loc_cpu();
    } else {
        chg_ipm(TMIN_INTPRI);
    }
    uint32_t modifiedFlag = THREAD_MEMB(thread_id, thread_flags) | flags;
    THREAD_MEMB(thread_id, thread_flags) = modifiedFlag;
    int wakeup = 0;
    switch (THREAD_MEMB(thread_id, flags_options)) {
        case THREAD_WAIT_FLAGS_OR:
            if (modifiedFlag & THREAD_MEMB(thread_id, wait_flags)) {
                wakeup = 1;
            }
            break;
        case THREAD_WAIT_FLAGS_AND:
            if ((modifiedFlag & THREAD_MEMB(thread_id, wait_flags)) == THREAD_MEMB(thread_id, wait_flags)) {
                wakeup = 1;
            }
            break;
        default:
            break;
    }
    if (wakeup) {
        THREAD_MEMB(thread_id, wait_flags) = 0;
        THREAD_MEMB(thread_id, flags_options) = THREAD_WAIT_FLAGS_NONE;
        if (isr) {
            unl_cpu();
        }
        wup_tsk(THREAD_MEMB(thread_id, id));
        if (! isr) {
            chg_ipm(TIPM_ENAALL);
            /* ここでディスパッチが起こった場合、新しいフラグを参照する */
            modifiedFlag = THREAD_MEMB(thread_id, wait_flags);
        }
        return modifiedFlag;
    }
    if (isr) {
        unl_cpu();
    } else {
        chg_ipm(TIPM_ENAALL);
    }
    return modifiedFlag;
}

/// Clear the specified Thread Flags of current running thread.
/// \param[in]     flags         specifies the flags of the thread that shall be cleared.
/// \return thread flags before clearing or error code if highest bit set.
uint32_t osThreadFlagsClear (uint32_t flags)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    if (flags & (1 << osRtxThreadFlagsLimit)) {
        return osErrorParameter;
    }
    osThreadId_t thread_id;
    get_inf((intptr_t *)&thread_id);
    loc_cpu();
    THREAD_MEMB(thread_id, thread_flags) &= ~(flags);
    unl_cpu();
    return osOK;
}

/// Get the current Thread Flags of current running thread.
/// \return current thread flags.
uint32_t osThreadFlagsGet (void)
{
    if (sns_ctx()) {
        return 0;
    }
    osThreadId_t thread_id;
    get_inf((intptr_t *)&thread_id);
    return THREAD_MEMB(thread_id, thread_flags);
}

/// Wait for one or more Thread Flags of the current running thread to become signaled.
/// \param[in]     flags         specifies the flags to wait for.
/// \param[in]     options       specifies flags options (osFlagsXxxx).
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return thread flags before clearing or error code if highest bit set.
uint32_t osThreadFlagsWait (uint32_t flags, uint32_t options, uint32_t timeout)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    int shouldReturnErrorCode = 0;
    if (flags & (1 << osRtxThreadFlagsLimit)) {
        shouldReturnErrorCode = 1;
        flags &= ~(1 << osRtxThreadFlagsLimit);
    }
    osThreadId_t thread_id;
    get_inf((intptr_t *)&thread_id);
    loc_cpu();
    if (options & osFlagsWaitAll) {
        if ((THREAD_MEMB(thread_id, thread_flags) & flags) == flags) {
            const uint32_t returnValue = THREAD_MEMB(thread_id, thread_flags);
            if ((options & osFlagsNoClear) == 0) {
                THREAD_MEMB(thread_id, thread_flags) &= ~(flags);
                unl_cpu();
                return returnValue;
            }
        }
    } else {
        if (THREAD_MEMB(thread_id, thread_flags) & flags) {
            const uint32_t returnValue = THREAD_MEMB(thread_id, thread_flags);
            if ((options & osFlagsNoClear) == 0) {
                THREAD_MEMB(thread_id, thread_flags) &= ~(flags);
                unl_cpu();
                return returnValue;
            }
        }
    }
    if (options & osFlagsWaitAll) {
        THREAD_MEMB(thread_id, flags_options) = THREAD_WAIT_FLAGS_AND;
    } else {
        THREAD_MEMB(thread_id, flags_options) = THREAD_WAIT_FLAGS_OR;
    }
    THREAD_MEMB(thread_id, wait_flags) = flags;
    unl_cpu();
    /*
     * ここで割り込みが発生して待っているフラグがセットされた場合でも、
     * 起床要求はキューイングされるので slp_tsk() からリターンできる。
     */
    if (timeout == 0) {
        slp_tsk();
    } else {
        if (tslp_tsk(1000 * timeout) == E_TMOUT) {
            loc_cpu();
            THREAD_MEMB(thread_id, wait_flags) = 0;
            THREAD_MEMB(thread_id, flags_options) = THREAD_WAIT_FLAGS_NONE;
            const uint32_t returnValue = THREAD_MEMB(thread_id, thread_flags);
            unl_cpu();
            if (shouldReturnErrorCode) {
                return osFlagsErrorTimeout;
            } else {
                return returnValue;
            }
        }
    }
    loc_cpu();
    const uint32_t returnValue = THREAD_MEMB(thread_id, thread_flags);
    if ((options & osFlagsNoClear) == 0) {
        THREAD_MEMB(thread_id, thread_flags) &= ~(flags);
    }
    unl_cpu();
    return returnValue;
}

/// Wait for Timeout (Time Delay).
/// \param[in]     ticks         \ref CMSIS_RTOS_TimeOutValue "time ticks" value
/// \return status code that indicates the execution status of the function.
osStatus_t osDelay (uint32_t ticks)
{
    if (ticks == 0) {
        return osErrorParameter;
    }
    switch (dly_tsk(ticks * 1000)) {
        case E_CTX: return osErrorISR;
        case E_OK:  return osOK;
        case E_PAR: return osErrorParameter;
        default: return osError;
    }
}

/// Wait until specified time.
/// \param[in]     ticks         absolute time in ticks
/// \return status code that indicates the execution status of the function.
osStatus_t osDelayUntil (uint32_t ticks)
{
    if (ticks == 0) {
        return osErrorParameter;
    }
    const RELTIM interval = fch_hrt() + ticks * 1000;
    switch (dly_tsk(interval)) {
        case E_CTX: return osErrorISR;
        case E_OK:  return osOK;
        case E_PAR: return osErrorParameter;
        default: return osError;
    }
}

#if RTOS_BRIDGE_NUM_TIMERS > 0

/// Create and Initialize a timer.
/// \param[in]     func          function pointer to callback function.
/// \param[in]     type          \ref osTimerOnce for one-shot or \ref osTimerPeriodic for periodic behavior.
/// \param[in]     argument      argument to the timer callback function.
/// \param[in]     attr          timer attributes; NULL: default values.
/// \return timer ID for reference by other functions or NULL in case of error.
osTimerId_t osTimerNew (osTimerFunc_t func, osTimerType_t type, void *argument, const osTimerAttr_t *attr)
{
    /* タイマはほぼ使わないようなので現状未サポート */
    return NULL;
}

/// Get name of a timer.
/// \param[in]     timer_id      timer ID obtained by \ref osTimerNew.
/// \return name as null-terminated string.
const char *osTimerGetName (osTimerId_t timer_id)
{
    return NULL;
}

/// Start or restart a timer.
/// \param[in]     timer_id      timer ID obtained by \ref osTimerNew.
/// \param[in]     ticks         \ref CMSIS_RTOS_TimeOutValue "time ticks" value of the timer.
/// \return status code that indicates the execution status of the function.
osStatus_t osTimerStart (osTimerId_t timer_id, uint32_t ticks)
{
    return osOK;
}

/// Stop a timer.
/// \param[in]     timer_id      timer ID obtained by \ref osTimerNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osTimerStop (osTimerId_t timer_id)
{
    return osOK;
}

/// Check if a timer is running.
/// \param[in]     timer_id      timer ID obtained by \ref osTimerNew.
/// \return 0 not running, 1 running.
uint32_t osTimerIsRunning (osTimerId_t timer_id)
{
    return 0;
}

/// Delete a timer.
/// \param[in]     timer_id      timer ID obtained by \ref osTimerNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osTimerDelete (osTimerId_t timer_id)
{
    return osOK;
}

#endif

#if RTOS_BRIDGE_NUM_EVENT_FLAGS > 0

#define EVTFLG_MEMB(id, memb) (((osRtxEventFlags_t *)id)->memb)

/// Create and Initialize an Event Flags object.
/// \param[in]     attr          event flags attributes; NULL: default values.
/// \return event flags ID for reference by other functions or NULL in case of error.
osEventFlagsId_t osEventFlagsNew (const osEventFlagsAttr_t *attr)
{
    if (attr == NULL || attr->cb_mem == NULL) {
        return NULL;
    }
    osRtxEventFlags_t *evtflg = (osRtxEventFlags_t *)attr->cb_mem;
    T_CFLG evtflgInfo;
    evtflgInfo.flgatr = TA_WMUL;
    evtflgInfo.iflgptn = 0;
    const ER_ID id = acre_flg(&evtflgInfo);
    if (id < 0) {
        return NULL;
    }
    evtflg->id = id;
    evtflg->name = attr->name;
    return (osEventFlagsId_t)evtflg;
}

/// Get name of an Event Flags object.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \return name as null-terminated string.
const char *osEventFlagsGetName (osEventFlagsId_t ef_id)
{
    return EVTFLG_MEMB(ef_id, name);
}

/// Set the specified Event Flags.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \param[in]     flags         specifies the flags that shall be set.
/// \return event flags after setting or error code if highest bit set.
uint32_t osEventFlagsSet (osEventFlagsId_t ef_id, uint32_t flags)
{
    switch (set_flg(EVTFLG_MEMB(ef_id, id), flags)) {
        case E_OK: return osOK;
        case E_ID: return osErrorParameter;
        default: return osError;
    }
}

/// Clear the specified Event Flags.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \param[in]     flags         specifies the flags that shall be cleared.
/// \return event flags before clearing or error code if highest bit set.
uint32_t osEventFlagsClear (osEventFlagsId_t ef_id, uint32_t flags)
{
    if (sns_ctx()) {
        /*
         * CMSIS-RTOSではISRからイベントフラグがクリアできるので、これは仕様に準拠できていない。
         * この場合にも対応するには例えば最高優先度タスクに移譲する等の手段が考えられる。
         */
        return osErrorISR;
    }
    switch (clr_flg(EVTFLG_MEMB(ef_id, id), flags)) {
        case E_OK: return osOK;
        case E_ID: return osErrorParameter;
        default: return osError;
    }
}

/// Get the current Event Flags.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \return current event flags.
uint32_t osEventFlagsGet (osEventFlagsId_t ef_id)
{
    if (sns_ctx()) {
        /* これも仕様に準拠できていないが、実装するにはカーネルの内部データにアクセスするしかない */
        return osErrorISR;
    }
    T_RFLG info;
    switch (ref_flg(EVTFLG_MEMB(ef_id, id), &info)) {
        case E_OK: return info.flgptn;
        default: return 0;
    }
}

/// Wait for one or more Event Flags to become signaled.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \param[in]     flags         specifies the flags to wait for.
/// \param[in]     options       specifies flags options (osFlagsXxxx).
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return event flags before clearing or error code if highest bit set.
uint32_t osEventFlagsWait (osEventFlagsId_t ef_id, uint32_t flags, uint32_t options, uint32_t timeout)
{
    if (sns_ctx()) {
        /* これも仕様に準拠できていない */
        return osErrorISR;
    }
    MODE mode;
    if (options & osFlagsWaitAll) {
        mode = TWF_ANDW;
    } else {
        mode = TWF_ORW;
    }
    FLGPTN returnedPattern;
    if (timeout == osWaitForever) {
        switch (wai_flg(EVTFLG_MEMB(ef_id, id), flags, mode, &returnedPattern)) {
            case E_OK:    break;
            case E_PAR:   return osErrorParameter;
            case E_NOEXS: return osErrorParameter;
            default: return osFlagsErrorUnknown;
        }
    } else if (timeout == 0) {
        switch (pol_flg(EVTFLG_MEMB(ef_id, id), flags, mode, &returnedPattern)) {
            case E_OK:    break;
            case E_TMOUT: return osFlagsErrorResource;
            case E_PAR:   return osErrorParameter;
            case E_NOEXS: return osErrorParameter;
            default: return osFlagsErrorUnknown;
        }
    } else {
        switch (twai_flg(EVTFLG_MEMB(ef_id, id), flags, mode, &returnedPattern, timeout * 1000)) {
            case E_OK:    break;
            case E_TMOUT: return osFlagsErrorTimeout;
            case E_PAR:   return osErrorParameter;
            case E_NOEXS: return osErrorParameter;
            default: return osFlagsErrorUnknown;
        }
    }
    if ((options & osFlagsNoClear) != 0) {
        /* 厳密には動作が異なってくるかもしれない */
        clr_flg(EVTFLG_MEMB(ef_id, id), flags);
    }
    return returnedPattern;
}

/// Delete an Event Flags object.
/// \param[in]     ef_id         event flags ID obtained by \ref osEventFlagsNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osEventFlagsDelete (osEventFlagsId_t ef_id)
{
    switch (del_flg(EVTFLG_MEMB(ef_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorParameter;
        default: return osError;
    }
}

#endif

#define MUTEX_FLAGS_NONE      0
#define MUTEX_FLAGS_RECURSIVE 1

#define MUTEX_MEMB(mutex_id, memb) (((osRtxMutex_t *)mutex_id)->memb)

/// Create and Initialize a Mutex object.
/// \param[in]     attr          mutex attributes; NULL: default values.
/// \return mutex ID for reference by other functions or NULL in case of error.
osMutexId_t osMutexNew (const osMutexAttr_t *attr)
{
    if (attr == NULL || attr->cb_mem == NULL) {
        return NULL;
    }
    osRtxMutex_t *mutex = (osRtxMutex_t *)attr->cb_mem;
    T_CMTX mutexInfo;
    if (attr->attr_bits & osMutexPrioInherit) {
        /* 厳密には少し違う */
        mutexInfo.mtxatr = TA_CEILING;
    } else {
        mutexInfo.mtxatr = 0;
    }
    mutexInfo.ceilpri = TMIN_TPRI + 1;
    /* osMutexRobust は必ず ON の動作になる */
    const ER_ID id = acre_mtx(&mutexInfo);
    if (id < 0) {
        return NULL;
    }
    mutex->id = id;
    mutex->name = attr->name;
    if (attr->attr_bits & osMutexRecursive) {
        mutex->flags = MUTEX_FLAGS_RECURSIVE;
        mutex->lock = 0;
    } else {
        mutex->flags = MUTEX_FLAGS_NONE;
    }
    return (osMutexId_t)mutex;
}

/// Get name of a Mutex object.
/// \param[in]     mutex_id      mutex ID obtained by \ref osMutexNew.
/// \return name as null-terminated string.
const char *osMutexGetName (osMutexId_t mutex_id)
{
    return MUTEX_MEMB(mutex_id, name);
}

/// Acquire a Mutex or timeout if it is locked.
/// \param[in]     mutex_id      mutex ID obtained by \ref osMutexNew.
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return status code that indicates the execution status of the function.
osStatus_t osMutexAcquire (osMutexId_t mutex_id, uint32_t timeout)
{
    switch (timeout) {
        case 0:
            switch (ploc_mtx(MUTEX_MEMB(mutex_id, id))) {
                case E_OK:
                {
                    osRtxThread_t *thread;
                    get_inf((intptr_t *)&thread);
                    MUTEX_MEMB(mutex_id, owner_thread) = thread;
                    return osOK;
                }
                case E_CTX:   return osErrorISR;
                case E_NOEXS: return osErrorParameter;
                case E_OBJ:
                    if (MUTEX_MEMB(mutex_id, flags) == MUTEX_FLAGS_RECURSIVE) {
                        MUTEX_MEMB(mutex_id, lock) += 1;
                        return osOK;
                    } else {
                        return osErrorResource;
                    }
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
        case osWaitForever:
            switch (loc_mtx(MUTEX_MEMB(mutex_id, id))) {
                case E_OK:
                {
                    osRtxThread_t *thread;
                    get_inf((intptr_t *)&thread);
                    MUTEX_MEMB(mutex_id, owner_thread) = thread;
                    return osOK;
                }
                case E_CTX:   return osErrorISR;
                case E_NOEXS: return osErrorParameter;
                case E_OBJ:
                    if (MUTEX_MEMB(mutex_id, flags) == MUTEX_FLAGS_RECURSIVE) {
                        MUTEX_MEMB(mutex_id, lock) += 1;
                        return osOK;
                    } else {
                        return osErrorResource;
                    }
                default: return osError;
            }
        default:
            switch (tloc_mtx(MUTEX_MEMB(mutex_id, id), timeout * 1000)) {
                case E_OK:
                {
                    osRtxThread_t *thread;
                    get_inf((intptr_t *)&thread);
                    MUTEX_MEMB(mutex_id, owner_thread) = thread;
                    return osOK;
                }
                case E_CTX:   return osErrorISR;
                case E_NOEXS: return osErrorParameter;
                case E_OBJ:
                    if (MUTEX_MEMB(mutex_id, flags) == MUTEX_FLAGS_RECURSIVE) {
                        MUTEX_MEMB(mutex_id, lock) += 1;
                        return osOK;
                    } else {
                        return osErrorResource;
                    }
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
    }
}

/// Release a Mutex that was acquired by \ref osMutexAcquire.
/// \param[in]     mutex_id      mutex ID obtained by \ref osMutexNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osMutexRelease (osMutexId_t mutex_id)
{
    if (sns_ctx()) {
        return osErrorISR;
    }
    if (MUTEX_MEMB(mutex_id, lock) > 1) {
        /* 本当は自タスクの mutex か確認するべき */
        MUTEX_MEMB(mutex_id, lock) -= 1;
        return osOK;
    }
    osStatus_t returnStatus;
    chg_ipm(TMIN_INTPRI);
    switch (unl_mtx(MUTEX_MEMB(mutex_id, id))) {
        case E_OK:
            MUTEX_MEMB(mutex_id, owner_thread) = NULL;
            returnStatus = osOK;
            break;
        case E_CTX:   returnStatus = osErrorISR; break;
        case E_ID:    returnStatus = osErrorParameter; break;
        case E_NOEXS: returnStatus = osErrorParameter; break;
        case E_OBJ:   returnStatus = osErrorResource; break;
        default: returnStatus = osError; break;
    }
    chg_ipm(TIPM_ENAALL);
    return returnStatus;
}

/// Get Thread which owns a Mutex object.
/// \param[in]     mutex_id      mutex ID obtained by \ref osMutexNew.
/// \return thread ID of owner thread or NULL when mutex was not acquired.
osThreadId_t osMutexGetOwner (osMutexId_t mutex_id)
{
    return MUTEX_MEMB(mutex_id, owner_thread);
}

/// Delete a Mutex object.
/// \param[in]     mutex_id      mutex ID obtained by \ref osMutexNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osMutexDelete (osMutexId_t mutex_id)
{
    switch (del_mtx(MUTEX_MEMB(mutex_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
}

#if RTOS_BRIDGE_NUM_SEMAPHORE > 0

#define SEM_MEMB(semaphroe_id, memb) (((osRtxSemaphore_t *)semaphore_id)->memb)

/// Create and Initialize a Semaphore object.
/// \param[in]     max_count     maximum number of available tokens.
/// \param[in]     initial_count initial number of available tokens.
/// \param[in]     attr          semaphore attributes; NULL: default values.
/// \return semaphore ID for reference by other functions or NULL in case of error.
osSemaphoreId_t osSemaphoreNew (uint32_t max_count, uint32_t initial_count, const osSemaphoreAttr_t *attr)
{
    if (attr == NULL || attr->cb_mem == NULL) {
        return NULL;
    }
    osRtxSemaphore_t *sem = (osRtxSemaphore_t *)attr->cb_mem;
    T_CSEM semInfo;
    semInfo.sematr = 0;
    semInfo.isemcnt = initial_count;
    semInfo.maxsem = max_count;
    const ER_ID id = acre_sem(&semInfo);
    if (id < 0) {
        return NULL;
    }
    sem->id = id;
    sem->name = attr->name;
    return (osSemaphoreId_t)sem;
}

/// Get name of a Semaphore object.
/// \param[in]     semaphore_id  semaphore ID obtained by \ref osSemaphoreNew.
/// \return name as null-terminated string.
const char *osSemaphoreGetName (osSemaphoreId_t semaphore_id)
{
    return SEM_MEMB(semaphore_id, name);
}

/// Acquire a Semaphore token or timeout if no tokens are available.
/// \param[in]     semaphore_id  semaphore ID obtained by \ref osSemaphoreNew.
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return status code that indicates the execution status of the function.
osStatus_t osSemaphoreAcquire (osSemaphoreId_t semaphore_id, uint32_t timeout)
{
    switch (timeout) {
        case 0:
            switch (pol_sem(SEM_MEMB(semaphore_id, id))) {
                case E_OK:    return osOK;
                case E_CTX:   return osErrorISR;
                case E_ID:    return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
        case osWaitForever:
            switch (wai_sem(SEM_MEMB(semaphore_id, id))) {
                case E_OK:    return osOK;
                case E_CTX:   return osErrorISR;
                case E_ID:    return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                default: return osError;
            }
        default:
            switch (twai_sem(SEM_MEMB(semaphore_id, id), timeout * 1000)) {
                case E_OK:    return osOK;
                case E_CTX:   return osErrorISR;
                case E_ID:    return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
    }
}

/// Release a Semaphore token up to the initial maximum count.
/// \param[in]     semaphore_id  semaphore ID obtained by \ref osSemaphoreNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osSemaphoreRelease (osSemaphoreId_t semaphore_id)
{
    switch (sig_sem(SEM_MEMB(semaphore_id, id))) {
        case E_OK:    return osOK;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_QOVR:  return osErrorResource;
        default: return osError;
    }
}

/// Get current Semaphore token count.
/// \param[in]     semaphore_id  semaphore ID obtained by \ref osSemaphoreNew.
/// \return number of tokens available.
uint32_t osSemaphoreGetCount (osSemaphoreId_t semaphore_id)
{
    T_RSEM rsem;
    if (ref_sem(SEM_MEMB(semaphore_id, id), &rsem) == E_OK) {
        return rsem.semcnt;
    }
    return 0;
}

/// Delete a Semaphore object.
/// \param[in]     semaphore_id  semaphore ID obtained by \ref osSemaphoreNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osSemaphoreDelete (osSemaphoreId_t semaphore_id)
{
    switch (del_sem(SEM_MEMB(semaphore_id, id))) {
        case E_OK:    return osOK;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
}

#endif

#if RTOS_BRIDGE_NUM_MEMORY_POOLS > 0

#define POOL_MEMB(mp_id, memb) (((osRtxMemoryPool_t *)mp_id)->memb)

/// Create and Initialize a Memory Pool object.
/// \param[in]     block_count   maximum number of memory blocks in memory pool.
/// \param[in]     block_size    memory block size in bytes.
/// \param[in]     attr          memory pool attributes; NULL: default values.
/// \return memory pool ID for reference by other functions or NULL in case of error.
osMemoryPoolId_t osMemoryPoolNew (uint32_t block_count, uint32_t block_size, const osMemoryPoolAttr_t *attr)
{
    if (attr == NULL || attr->cb_mem == NULL || attr->mp_mem == NULL) {
        return NULL;
    }
    if (block_count > RTOS_BRIDGE_MAX_BLOCK_COUNT) {
        return NULL;
    }
    osRtxMemoryPool_t *pool = (osRtxMemoryPool_t *)attr->cb_mem;
    void *mpfmb;
    get_mpf(__RTOS_BRIDGE_MPFMB_POOL, &mpfmb);
    T_CMPF memInfo;
    memInfo.mpfatr = 0;
    memInfo.blkcnt = block_count;
    memInfo.blksz = block_size;
    memInfo.mpf = attr->mp_mem;
    memInfo.mpfmb = mpfmb;
    const ER_ID id = acre_mpf(&memInfo);
    if (id < 0) {
        return NULL;
    }
    pool->id = id;
    pool->name = attr->name;
    pool->mp_info.max_blocks = block_count;
    pool->mp_info.block_size = block_size;
    return (osMemoryPoolId_t)pool;
}

/// Get name of a Memory Pool object.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return name as null-terminated string.
const char *osMemoryPoolGetName (osMemoryPoolId_t mp_id)
{
    return POOL_MEMB(mp_id, name);
}

/// Allocate a memory block from a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return address of the allocated memory block or NULL in case of no memory is available.
void *osMemoryPoolAlloc (osMemoryPoolId_t mp_id, uint32_t timeout)
{
    /* ISR から取得できないため仕様に準拠できていない */
    void *block;
    switch (timeout) {
        case 0:
            if (pget_mpf(POOL_MEMB(mp_id, id), &block) == E_OK) {
                return block;
            }
            break;
        case osWaitForever:
            if (get_mpf(POOL_MEMB(mp_id, id), &block) == E_OK) {
                return block;
            }
            break;
        default:
            if (tget_mpf(POOL_MEMB(mp_id, id), &block, timeout * 1000) == E_OK) {
                return block;
            }
    }
    return NULL;
}

/// Return an allocated memory block back to a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \param[in]     block         address of the allocated memory block to be returned to the memory pool.
/// \return status code that indicates the execution status of the function.
osStatus_t osMemoryPoolFree (osMemoryPoolId_t mp_id, void *block)
{
    /* ISR から取得できないため仕様に準拠できていない */
    switch (rel_mpf(POOL_MEMB(mp_id, id), block)) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorResource;
        case E_ID:    return osErrorParameter;
        case E_PAR:   return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        default: return osError;
    }
}

/// Get maximum number of memory blocks in a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return maximum number of memory blocks.
uint32_t osMemoryPoolGetCapacity (osMemoryPoolId_t mp_id)
{
    return POOL_MEMB(mp_id, mp_info.max_blocks);
}

/// Get memory block size in a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return memory block size in bytes.
uint32_t osMemoryPoolGetBlockSize (osMemoryPoolId_t mp_id)
{
    return POOL_MEMB(mp_id, mp_info.block_size);
}

/// Get number of memory blocks used in a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return number of memory blocks used.
uint32_t osMemoryPoolGetCount (osMemoryPoolId_t mp_id)
{
    T_RMPF rmpf;
    if (ref_mpf(POOL_MEMB(mp_id, id), &rmpf) == E_OK) {
        return osMemoryPoolGetCapacity(mp_id) - rmpf.fblkcnt;
    }
    return 0;
}

/// Get number of memory blocks available in a Memory Pool.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return number of memory blocks available.
uint32_t osMemoryPoolGetSpace (osMemoryPoolId_t mp_id)
{
    T_RMPF rmpf;
    if (ref_mpf(POOL_MEMB(mp_id, id), &rmpf) == E_OK) {
        return rmpf.fblkcnt;
    }
    return 0;
}

/// Delete a Memory Pool object.
/// \param[in]     mp_id         memory pool ID obtained by \ref osMemoryPoolNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osMemoryPoolDelete (osMemoryPoolId_t mp_id)
{
    switch (del_mpf(POOL_MEMB(mp_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
}

#endif

#if RTOS_BRIDGE_NUM_MSG_QUEUE > 0

#define QUEUE_MEMB(mq_id, memb) (((osRtxMessageQueue_t *)mq_id)->memb)

/// Create and Initialize a Message Queue object.
/// \param[in]     msg_count     maximum number of messages in queue.
/// \param[in]     msg_size      maximum message size in bytes.
/// \param[in]     attr          message queue attributes; NULL: default values.
/// \return message queue ID for reference by other functions or NULL in case of error.
osMessageQueueId_t osMessageQueueNew (uint32_t msg_count, uint32_t msg_size, const osMessageQueueAttr_t *attr)
{
    if (attr == NULL || attr->cb_mem == NULL || attr->mq_mem == NULL) {
        return NULL;
    }
    if (msg_size != sizeof(intptr_t)) {
        /*
         * CMSIS-RTOS のメッセージキューはデータをコピー渡しするので、内部にメモリプールを持っている。
         * 真面目に実装すると面倒だが、C++ API ではデータとしてポインタのみを扱っているので、ポインタ
         * 以外はサポートしないこととする。
         */
        return NULL;
    }
    osRtxMessageQueue_t *queue = (osRtxMessageQueue_t *)attr->cb_mem;
    T_CPDQ queueInfo;
    queueInfo.pdqatr = 0;
    queueInfo.pdqcnt = msg_count;
    queueInfo.maxdpri = TMAX_DPRI;
    queueInfo.pdqmb = attr->mq_mem;
    const ER_ID id = acre_pdq(&queueInfo);
    if (id < 0) {
        return NULL;
    }
    queue->id = id;
    queue->name = attr->name;
    queue->msg_count = msg_count;
    queue->msg_size = msg_size;
    return (osMessageQueueId_t)queue;
}

/// Get name of a Message Queue object.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return name as null-terminated string.
const char *osMessageQueueGetName (osMessageQueueId_t mq_id)
{
    return QUEUE_MEMB(mq_id, name);
}

/// Put a Message into a Queue or timeout if Queue is full.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \param[in]     msg_ptr       pointer to buffer with message to put into a queue.
/// \param[in]     msg_prio      message priority.
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return status code that indicates the execution status of the function.
osStatus_t osMessageQueuePut (osMessageQueueId_t mq_id, const void *msg_ptr, uint8_t msg_prio, uint32_t timeout)
{
    if (msg_prio > TMAX_DPRI) {
        msg_prio = TMAX_DPRI; /* 0 ~ 255 を 0 ~ TMAX_DPRI にマップする実装もあり */
    }
    msg_prio = TMAX_DPRI - msg_prio;
    switch (timeout) {
        case 0:
            switch (psnd_pdq(QUEUE_MEMB(mq_id, id), *(intptr_t *)msg_ptr, msg_prio)) {
                case E_OK:    return osOK;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
        case osWaitForever:
            switch (snd_pdq(QUEUE_MEMB(mq_id, id), *(intptr_t *)msg_ptr, msg_prio)) {
                case E_OK:    return osOK;
                case E_CTX:   return osErrorParameter;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorTimeout;
                default: return osError;
            }
        default:
            switch (tsnd_pdq(QUEUE_MEMB(mq_id, id), *(intptr_t *)msg_ptr, msg_prio, timeout * 1000)) {
                case E_OK:    return osOK;
                case E_CTX:   return osErrorParameter;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorTimeout;
                default: return osError;
            }
    }
}

/// Get a Message from a Queue or timeout if Queue is empty.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \param[out]    msg_ptr       pointer to buffer for message to get from a queue.
/// \param[out]    msg_prio      pointer to buffer for message priority or NULL.
/// \param[in]     timeout       \ref CMSIS_RTOS_TimeOutValue or 0 in case of no time-out.
/// \return status code that indicates the execution status of the function.
osStatus_t osMessageQueueGet (osMessageQueueId_t mq_id, void *msg_ptr, uint8_t *msg_prio, uint32_t timeout)
{
    PRI prio;
    if (msg_prio == NULL) {
        msg_prio = (uint8_t *)&prio;
    }
    switch (timeout) {
        case 0:
            switch (prcv_pdq(QUEUE_MEMB(mq_id, id), (intptr_t *)msg_ptr, (PRI *)msg_prio)) {
                case osOK:
                    *msg_prio = TMAX_DPRI - *msg_prio;
                    return osOK;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorResource;
                default: return osError;
            }
        case osWaitForever:
            switch (rcv_pdq(QUEUE_MEMB(mq_id, id), (intptr_t *)msg_ptr, (PRI *)msg_prio)) {
                case E_OK:
                    *msg_prio = TMAX_DPRI - *msg_prio;
                    return osOK;
                case E_CTX:   return osErrorParameter;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorTimeout;
                default: return osError;
            }
        default:
            switch (trcv_pdq(QUEUE_MEMB(mq_id, id), (intptr_t *)msg_ptr, (PRI *)msg_prio, timeout * 1000)) {
                case E_OK:
                    *msg_prio = TMAX_DPRI - *msg_prio;
                    return osOK;
                case E_CTX:   return osErrorParameter;
                case E_ID:    return osErrorParameter;
                case E_PAR:   return osErrorParameter;
                case E_NOEXS: return osErrorParameter;
                case E_TMOUT: return osErrorTimeout;
                default: return osError;
            }
    }
}

/// Get maximum number of messages in a Message Queue.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return maximum number of messages.
uint32_t osMessageQueueGetCapacity (osMessageQueueId_t mq_id)
{
    return QUEUE_MEMB(mq_id, msg_count);
}

/// Get maximum message size in a Message Queue.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return maximum message size in bytes.
uint32_t osMessageQueueGetMsgSize (osMessageQueueId_t mq_id)
{
    return QUEUE_MEMB(mq_id, msg_size);
}

/// Get number of queued messages in a Message Queue.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return number of queued messages.
uint32_t osMessageQueueGetCount (osMessageQueueId_t mq_id)
{
    T_RPDQ rpdq;
    if (ref_pdq(QUEUE_MEMB(mq_id, id), &rpdq) == E_OK) {
        return rpdq.spdqcnt;
    }
    return 0;
}

/// Get number of available slots for messages in a Message Queue.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return number of available slots for messages.
uint32_t osMessageQueueGetSpace (osMessageQueueId_t mq_id)
{
    return osMessageQueueGetCapacity(mq_id) - osMessageQueueGetCount(mq_id);
}

/// Reset a Message Queue to initial empty state.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osMessageQueueReset (osMessageQueueId_t mq_id)
{
    switch (ini_pdq(QUEUE_MEMB(mq_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        default: return osError;
    }
}

/// Delete a Message Queue object.
/// \param[in]     mq_id         message queue ID obtained by \ref osMessageQueueNew.
/// \return status code that indicates the execution status of the function.
osStatus_t osMessageQueueDelete (osMessageQueueId_t mq_id)
{
    switch (del_pdq(QUEUE_MEMB(mq_id, id))) {
        case E_OK:    return osOK;
        case E_CTX:   return osErrorISR;
        case E_ID:    return osErrorParameter;
        case E_NOEXS: return osErrorParameter;
        case E_OBJ:   return osErrorResource;
        default: return osError;
    }
}

#endif
