#include <Arduino.h>

// Arduino gives loopTask an 8KB stack. tinyengine itself is shallow, but pntr's drawing and
// the default font both want room, and a stack overflow here reports as a crash with a
// backtrace that points nowhere useful.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Arduino-ESP32 owns app_main() and calls setup()/loop() from its own task. Both have C++
// linkage, so this shim hands control to the C application in demo_esp32.c.

extern "C" bool pntr_app_esp32_setup(void);
extern "C" bool pntr_app_esp32_loop(void);

static bool running = false;

void setup() {
  running = pntr_app_esp32_setup();
}

void loop() {
  if (running) {
    running = pntr_app_esp32_loop();
  }
}
