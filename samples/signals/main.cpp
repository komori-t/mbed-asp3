#include "mbed.h"
#include "rtos.h"

Thread thread;
DigitalOut led1(LED1);

#define STOP_FLAG 1

// Blink function toggles the led in a long running loop
void blink(DigitalOut *led) {
    while (!ThisThread::flags_wait_any_for(STOP_FLAG, 1s)) {
        *led = !*led;
    }
}

// Spawns a thread to run blink for 5 seconds
int main() {
    thread.start(callback(blink, &led1));
    ThisThread::sleep_for(5s);
    thread.flags_set(STOP_FLAG);
    thread.join();
}
