#include "WebCuda/WebCudaBrowserHost.h"
#include <emscripten.h>
using namespace WebCudaBrowser;
namespace { int Frames=0; double Last=0; bool Ordered=true;
bool Frame(double Time,void*) {Ordered &= Time>=Last; Last=Time; return ++Frames<5;}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int host_init() {return Initialize();}
EMSCRIPTEN_KEEPALIVE int host_resize(int W,int H) {return Resize(W,H);}
EMSCRIPTEN_KEEPALIVE int host_begin() {Frames=0;Last=0;Ordered=true;return StartFrames(Frame,nullptr);}
EMSCRIPTEN_KEEPALIVE int host_frames() {return Ordered?Frames:-1;}
EMSCRIPTEN_KEEPALIVE void host_stop() {StopFrames();}
EMSCRIPTEN_KEEPALIVE void host_shutdown() {Shutdown();}
EMSCRIPTEN_KEEPALIVE int host_poll() {
    Event E; if(!Poll(E))return -1;
    EM_ASM({globalThis.events.push({kind:$0,a:$1,b:$2,x:$3,y:$4,repeat:!!$5});},int(E.Type),E.A,E.B,E.X,E.Y,E.Repeat);
    return int(E.Type);
}
EMSCRIPTEN_KEEPALIVE void host_style() {Title("WebCuda host validation");Cursor("crosshair");Focus();}
}
