#include "mbed.h"
#include "rtos.h"
 
Mutex stdio_mutex;
 
void notify(const char* name, int state) {
    stdio_mutex.lock();
    printf("%s: %d\r\n", name, state);
    stdio_mutex.unlock();
}
 
void test_thread(void const *args) {
    while (true) {
        notify((const char*)args, 0); ThisThread::sleep_for(1s);
        notify((const char*)args, 1); ThisThread::sleep_for(1s);
    }
}
 
int main() {
    Thread t2;
    Thread t3;
 
    t2.start(callback(test_thread, (void *)"Th 2"));
    t3.start(callback(test_thread, (void *)"Th 3"));
 
    test_thread((void *)"Th 1");
}
