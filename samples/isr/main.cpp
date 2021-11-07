#include "mbed.h"
#include "rtos.h"
 
Queue<uint32_t, 5> queue;
 
DigitalOut myled(LED1);
 
void queue_isr() {
    queue.try_put((uint32_t*)2);
    myled = !myled;
}
 
void queue_thread(void) {
    while (true) {
        queue.put((uint32_t*)1);
        ThisThread::sleep_for(1s);
    }
}
 
int main (void) {
    Thread thread;
    thread.start(queue_thread);
    
    Ticker ticker;
    ticker.attach(queue_isr, 1.0);
    
    while (true) {
        osEvent evt = queue.get();
        if (evt.status != osEventMessage) {
            printf("queue->get() returned %02x status\r\n", evt.status);
        } else {
            printf("queue->get() returned %d\r\n", evt.value.v);
        }
    }
}
