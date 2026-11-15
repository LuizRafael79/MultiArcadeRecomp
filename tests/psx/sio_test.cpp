#include "sio.h"

#include <cassert>
#include <cstdint>

namespace {
struct Loopback { int data = 1; };
void clock_pin(void *, int) {}
void data_pin(void *opaque, int value) { static_cast<Loopback *>(opaque)->data = value; }
int read_pin(void *opaque) { return static_cast<Loopback *>(opaque)->data; }
}

int main()
{
    Loopback loop;
    PsxBus pins{};
    pins.opaque = &loop;
    pins.serial_clock = clock_pin;
    pins.serial_data = data_pin;
    pins.serial_read = read_pin;

    multiarcaderecomp::psx::Sio0 sio;
    sio.write(2, (1u << 16) | 1u); // 8-bit TX enabled, divide by 1
    sio.write(3, 1u << 16);        // baud period = 1
    sio.write(0, 0xa5);
    sio.advance(32, &pins);
    assert((sio.read(1) & 0x02) != 0);
    assert(sio.read(0) == 0xa5);
    assert(sio.transmitted_bytes() == 1);
    assert(sio.received_bytes() == 1);
    assert((sio.read(1) & 0x02) == 0);

    // ZN-2's 100 MHz main CPU advances the 33.8688 MHz serial clock at
    // roughly one SIO bit per three CPU cycles (prescaler=baud=1).
    Loopback zn2_loop;
    pins.opaque = &zn2_loop;
    multiarcaderecomp::psx::Sio0 zn2_sio;
    zn2_sio.write(2, (1u << 16) | 1u);
    zn2_sio.write(3, 1u << 16);
    zn2_sio.write(0, 0x5a);
    zn2_sio.advance(20, &pins, 100'000'000);
    assert(zn2_sio.transmitted_bytes() == 0);
    zn2_sio.advance(24, &pins, 100'000'000);
    assert(zn2_sio.transmitted_bytes() == 1);
    assert(zn2_sio.read(0) == 0x5a);

    // ZN-1's 67.7376 MHz CPU has a 1:2 ratio to the serial reference.
    Loopback zn1_loop;
    pins.opaque = &zn1_loop;
    multiarcaderecomp::psx::Sio0 zn1_sio;
    zn1_sio.write(2, (1u << 16) | 1u);
    zn1_sio.write(3, 1u << 16);
    zn1_sio.write(0, 0xa6);
    zn1_sio.advance(14, &pins, 67'737'600);
    assert(zn1_sio.transmitted_bytes() == 0);
    zn1_sio.advance(16, &pins, 67'737'600);
    assert(zn1_sio.transmitted_bytes() == 1);
    assert(zn1_sio.read(0) == 0xa6);
}
