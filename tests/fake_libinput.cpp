// Test double below the actual LD_PRELOAD layer. The production collector is unchanged.
#include <libinput.h>
#include <cstdint>
struct FakeDevice { const char *name; };
struct FakeEvent { FakeDevice *device; double dx,dy,rx,ry;std::uint64_t time; };
extern "C" {
double libinput_event_pointer_get_dx(libinput_event_pointer *p) {return reinterpret_cast<FakeEvent *>(p)->dx;}
double libinput_event_pointer_get_dy(libinput_event_pointer *p) {return reinterpret_cast<FakeEvent *>(p)->dy;}
double libinput_event_pointer_get_dx_unaccelerated(libinput_event_pointer *p) {return reinterpret_cast<FakeEvent *>(p)->rx;}
double libinput_event_pointer_get_dy_unaccelerated(libinput_event_pointer *p) {return reinterpret_cast<FakeEvent *>(p)->ry;}
std::uint64_t libinput_event_pointer_get_time_usec(libinput_event_pointer *p) {return reinterpret_cast<FakeEvent *>(p)->time;}
libinput_event *libinput_event_pointer_get_base_event(libinput_event_pointer *p) {return reinterpret_cast<libinput_event *>(p);}
libinput_event_type libinput_event_get_type(libinput_event *) {return LIBINPUT_EVENT_POINTER_MOTION;}
libinput_device *libinput_event_get_device(libinput_event *p) {return reinterpret_cast<libinput_device *>(reinterpret_cast<FakeEvent *>(p)->device);}
const char *libinput_device_get_sysname(libinput_device *p) {return reinterpret_cast<FakeDevice *>(p)->name;}
void libinput_event_destroy(libinput_event *) {}
}
