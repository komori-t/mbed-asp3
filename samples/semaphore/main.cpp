#include "mbed.h"
#include "rtos.h"
 
Semaphore two_slots(2);
 
void test_thread(void const *name) {
    while (true) {
        two_slots.acquire();
        printf("%s\r\n", (const char*)name);
        ThisThread::sleep_for(1s);
        two_slots.release();
    }
}
 
int main (void) {
    Thread t2;
    Thread t3;
 
    t2.start(callback(test_thread, (void *)"Th 2"));
    t3.start(callback(test_thread, (void *)"Th 3"));
 
    test_thread((void *)"Th 1");
}
