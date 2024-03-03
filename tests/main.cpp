#include "libserial/SerialPort.h"
#include <emscripten.h>

using namespace LibSerial;

extern "C" {
    int main() {
        return 0;
    }

    EMSCRIPTEN_KEEPALIVE
    int open() {
        SerialPort serial_port;
        serial_port.Open("/dev/null");
        return 0;
    }


}

