#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
int main() {
    auto *display=XOpenDisplay(nullptr);if(!display){std::fputs("无法连接 XWayland。\n",stderr);return 1;}
    int opcode,event,error;
    if(!XQueryExtension(display,"XInputExtension",&opcode,&event,&error))return 1;
    int major=2,minor=0;if(XIQueryVersion(display,&major,&minor)!=Success)return 1;
    const auto root=DefaultRootWindow(display);
    auto window=XCreateSimpleWindow(display,root,0,0,1280,800,0,0,0x202830);
    XStoreName(display,window,"PointerAccel XWayland raw probe");
    XMapRaised(display,window);XSync(display,False);
    bool viewable=false;
    for(int i=0;i<500;++i) {
        XWindowAttributes attributes;XGetWindowAttributes(display,window,&attributes);
        if(attributes.map_state==IsViewable){viewable=true;break;}
        usleep(10000);
    }
    if(!viewable){std::fputs("窗口未映射。\n",stderr);return 1;}
    XSetInputFocus(display,window,RevertToParent,CurrentTime);
    unsigned char bits[XIMaskLen(XI_RawMotion)]{};XISetMask(bits,XI_RawMotion);
    XIEventMask mask{XIAllMasterDevices,sizeof(bits),bits};XISelectEvents(display,root,&mask,1);XSync(display,False);
    std::puts("READY");std::fflush(stdout);
    while(true) {
        XEvent e;XNextEvent(display,&e);
        if(e.xcookie.type!=GenericEvent || e.xcookie.extension!=opcode || !XGetEventData(display,&e.xcookie))continue;
        if(e.xcookie.evtype==XI_RawMotion) {
            const auto *raw=static_cast<XIRawEvent *>(e.xcookie.data);double x=0,y=0;int index=0;
            for(int i=0;i<raw->valuators.mask_len*8;++i)if(XIMaskIsSet(raw->valuators.mask,i)) {
                if(i==0)x=raw->raw_values[index];
                if(i==1)y=raw->raw_values[index];
                ++index;
            }
            std::printf("{\"raw_x\":%.8f,\"raw_y\":%.8f}\n",x,y);std::fflush(stdout);
        }
        XFreeEventData(display,&e.xcookie);
    }
}
