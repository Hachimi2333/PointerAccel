#include <libinput.h>

extern "C" double consume_x(libinput_event_pointer* p)
{
	return libinput_event_pointer_get_dx(p);
}

extern "C" double consume_y(libinput_event_pointer* p)
{
	return libinput_event_pointer_get_dy(p);
}

extern "C" void consume_destroy(libinput_event* p)
{
	libinput_event_destroy(p);
}
