#include "mbed.h"

extern "C" {
    #include "kernel_rename.h"
    #include "t_stddef.h"
    #include "target_timer.h"
    void signal_time(void);
}

static Timer timer;
static Timeout timeout;

void target_hrt_initialize(intptr_t exinf)
{
    timer.start();
}

void target_hrt_terminate(intptr_t exinf)
{
    timer.stop();
    timeout.detach();
}

HRTCNT target_hrt_get_current(void)
{
    return static_cast<HRTCNT>(timer.elapsed_time().count());
}

void target_hrt_raise_event(void)
{
    timeout.attach(target_hrt_handler, std::chrono::microseconds(0));
}

void target_hrt_set_event(HRTCNT hrtcnt)
{
    timeout.attach(target_hrt_handler, std::chrono::microseconds(hrtcnt));
}

void target_hrt_handler(void)
{
    signal_time();
}
