#include "WebCuda/WebCudaBrowserHost.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

namespace WebCudaBrowser
{
namespace {
constexpr const char* Canvas="#canvas";
Event Queue[512]; unsigned Read=0, Write=0;
bool Ready=false;
FrameCallback Tick=nullptr; void* TickUser=nullptr; bool FramePending=false;
void Push(Event E) {
    if (Write-Read == 512) { Read=Write; Queue[Write++ % 512]={Kind::Reset}; }
    Queue[Write++ % 512]=E;
}
EM_JS(bool, HasCanvas, (), {const c=document.querySelector('#canvas'); if(!c)return false; c.tabIndex=0; return true;});
EM_JS(bool, HasFocus, (), {return !document.hidden && document.hasFocus();});
EM_JS(void, FocusCanvas, (), {document.querySelector('#canvas')?.focus();});
EM_JS(void, ShowCanvas, (bool show), {document.querySelector('#canvas').style.visibility=show?'visible':'hidden';});
EM_JS(void, SetCursor, (const char* style), {document.querySelector('#canvas').style.cursor=UTF8ToString(Number(style));});
EM_JS(void, SetTitle, (const char* title), {document.title=UTF8ToString(Number(title));});
EM_JS(double, CanvasLeft, (), {return document.querySelector('#canvas').getBoundingClientRect().left;});
EM_JS(double, CanvasTop, (), {return document.querySelector('#canvas').getBoundingClientRect().top;});
EM_BOOL Mouse(int Type,const EmscriptenMouseEvent* E,void*) {
    Event Out{Type==EMSCRIPTEN_EVENT_MOUSEDOWN?Kind::Down:Type==EMSCRIPTEN_EVENT_MOUSEUP?Kind::Up:Kind::Move};
    Out.A=E->button; Out.X=E->targetX; Out.Y=E->targetY;
    // Mouse-up is captured on window so dragging outside cannot leave a held
    // button behind. Deprecated canvasX/Y are not populated by modern Emscripten.
    if(Type==EMSCRIPTEN_EVENT_MOUSEUP){Out.X=E->clientX-CanvasLeft();Out.Y=E->clientY-CanvasTop();}
    // Coordinates use CSS pixels, matching the canvas window's logical size.
    if(Type==EMSCRIPTEN_EVENT_MOUSEDOWN) FocusCanvas();
    Push(Out); return true;
}
EM_BOOL Wheel(int,const EmscriptenWheelEvent* E,void*) {Push({Kind::Wheel,0,0,-E->deltaY/(E->deltaMode==0?100.0:3.0),0});return true;}
EM_BOOL Key(int Type,const EmscriptenKeyboardEvent* E,void*) {
    if(Type==EMSCRIPTEN_EVENT_KEYPRESS) {Push({Kind::Character,int(E->charCode),0,0,0,bool(E->repeat)});return true;}
    int Code=E->keyCode;
    if(Code>=16 && Code<=18)Code=160+(Code-16)*2+(E->location==2?1:0);
    Push({Type==EMSCRIPTEN_EVENT_KEYDOWN?Kind::KeyDown:Kind::KeyUp,Code,0,0,0,bool(E->repeat)});
    // Cancelling printable keydown also suppresses keypress in Chromium.
    // Let it reach the character callback, which prevents the default action.
    if(Type==EMSCRIPTEN_EVENT_KEYDOWN && (std::strlen(E->key)==1 || static_cast<unsigned char>(E->key[0])>=128))return false;
    // Preserve browser shortcuts; ordinary focused-canvas keys belong to UE.
    return !(E->ctrlKey || E->metaKey || E->keyCode==9 || E->keyCode==27 || E->keyCode>=112);
}
EM_BOOL FocusEvent(int Type,const EmscriptenFocusEvent*,void*) {Push({Kind::Focus,Type==EMSCRIPTEN_EVENT_FOCUS}); if(Type==EMSCRIPTEN_EVENT_BLUR)Push({Kind::Reset}); return false;}
EM_BOOL Frame(double Time,void*) {
    FramePending=false;
    auto Callback=Tick;
    if(!Callback)return false;
    if(!Callback(Time,TickUser)){Tick=nullptr;return false;}
    FramePending=true; return true;
}
}
bool Initialize() {
    if(Ready)return true;
    if(!HasCanvas())return false;
    if(emscripten_set_mousedown_callback(Canvas,nullptr,false,Mouse)!=EMSCRIPTEN_RESULT_SUCCESS)return false;
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,nullptr,false,Mouse);
    emscripten_set_mousemove_callback(Canvas,nullptr,false,Mouse);
    emscripten_set_wheel_callback(Canvas,nullptr,false,Wheel);
    emscripten_set_keydown_callback(Canvas,nullptr,false,Key);
    emscripten_set_keyup_callback(Canvas,nullptr,false,Key);
    emscripten_set_keypress_callback(Canvas,nullptr,false,Key);
    emscripten_set_focus_callback(Canvas,nullptr,false,FocusEvent);
    emscripten_set_blur_callback(Canvas,nullptr,false,FocusEvent);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,nullptr,false,FocusEvent);
    Read=Write=0; Ready=true; return true;
}
void Shutdown() {
    StopFrames(); if(!Ready)return;
    emscripten_set_mousedown_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,nullptr,false,nullptr);
    emscripten_set_mousemove_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_wheel_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_keydown_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_keyup_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_keypress_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_focus_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_blur_callback(Canvas,nullptr,false,nullptr);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,nullptr,false,nullptr);
    Ready=false; Read=Write=0;
}
bool Poll(Event& Out) {if(Read==Write)return false;Out=Queue[Read++ % 512];return true;}
void Size(int& W,int& H) {emscripten_get_canvas_element_size(Canvas,&W,&H);}
bool Resize(int W,int H) {
    if(W<=0||H<=0||W>16384||H>16384)return false;
    if(emscripten_set_canvas_element_size(Canvas,W,H)!=EMSCRIPTEN_RESULT_SUCCESS)return false;
    emscripten_set_element_css_size(Canvas,W,H); Push({Kind::Resize,W,H});return true;
}
bool Foreground(){return HasFocus();}
void Focus(){FocusCanvas();}
void Visible(bool Show){ShowCanvas(Show);}
void Cursor(const char* Style){SetCursor(Style);}
void Title(const char* Text){SetTitle(Text);}
bool StartFrames(FrameCallback Callback,void* User) {
    if(!Ready||!Callback||Tick||FramePending)return false;
    Tick=Callback; TickUser=User; FramePending=true;
    emscripten_request_animation_frame_loop(Frame,nullptr);
    return true;
}
void StopFrames(){Tick=nullptr;TickUser=nullptr;}
}
