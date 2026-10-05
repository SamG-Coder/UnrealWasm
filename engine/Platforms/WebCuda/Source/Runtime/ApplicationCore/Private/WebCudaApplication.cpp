#include "WebCuda/WebCudaPlatformApplicationMisc.h"
#include "WebCuda/WebCudaBrowserHost.h"
#include "GenericPlatform/ICursor.h"
#include "Containers/Set.h"
#include "Containers/StringConv.h"

class FWebCudaCursor final : public ICursor
{
public:
    FVector2D Position=FVector2D::ZeroVector;
    EMouseCursor::Type Type=EMouseCursor::Default;
    FVector2D GetPosition() const override { return Position; }
    void SetPosition(int32 X,int32 Y) override { Position=FVector2D(X,Y); } // software cursor; browser cannot warp OS pointer
    void SetType(EMouseCursor::Type InType) override {Type=InType;WebCudaBrowser::Cursor(Type==EMouseCursor::None?"none":Type==EMouseCursor::TextEditBeam?"text":Type==EMouseCursor::Hand?"pointer":"default");}
    EMouseCursor::Type GetType() const override {return Type;}
    void GetSize(int32& W,int32& H) const override {W=H=16;}
    void Show(bool Visible) override {if(Visible)SetType(Type);else WebCudaBrowser::Cursor("none");}
    void Lock(const RECT*) override {} // pointer lock requires a user gesture; not advertised
    void SetTypeShape(EMouseCursor::Type,void*) override {}
};
class FWebCudaWindow final : public FGenericWindow
{
    bool bVisible=false;
public:
    void Initialize(const TSharedRef<FGenericWindowDefinition>& InDefinition) {Definition=InDefinition;ReshapeWindow(0,0,InDefinition->WidthDesiredOnScreen,InDefinition->HeightDesiredOnScreen);}
    void ReshapeWindow(int32,int32,int32 W,int32 H) override {checkf(WebCudaBrowser::Resize(W,H),TEXT("Invalid WebCuda canvas dimensions"));}
    bool GetFullScreenInfo(int32& X,int32& Y,int32& W,int32& H) const override {X=Y=0;WebCudaBrowser::Size(W,H);return true;}
    bool GetRestoredDimensions(int32& X,int32& Y,int32& W,int32& H) override {return GetFullScreenInfo(X,Y,W,H);}
    void Show() override {bVisible=true;WebCudaBrowser::Visible(true);}
    void Hide() override {bVisible=false;WebCudaBrowser::Visible(false);}
    bool IsVisible() const override {return bVisible;}
    void SetWindowFocus() override {WebCudaBrowser::Focus();}
    void BringToFront(bool) override {SetWindowFocus();}
    void Destroy() override {Hide();}
    void SetText(const TCHAR* Text) override {WebCudaBrowser::Title(TCHAR_TO_UTF8(Text));}
    void* GetOSWindowHandle() const override {return const_cast<char*>("#canvas");}
    EWindowMode::Type GetWindowMode() const override {return EWindowMode::Windowed;}
};
class FWebCudaApplication final : public GenericApplication
{
    TSharedPtr<FWebCudaWindow> Window;
    TSet<int32> HeldKeys;
    TSet<int32> HeldButtons;
    static EMouseButtons::Type Button(int Code) {return Code==0?EMouseButtons::Left:Code==1?EMouseButtons::Middle:Code==2?EMouseButtons::Right:Code==3?EMouseButtons::Thumb01:EMouseButtons::Thumb02;}
public:
    FWebCudaApplication():GenericApplication(MakeShared<FWebCudaCursor>()) {}
    ~FWebCudaApplication() override {WebCudaBrowser::Shutdown();}
    TSharedRef<FGenericWindow> MakeWindow() override {checkf(!Window, TEXT("WebCuda currently supports one canvas window"));Window=MakeShared<FWebCudaWindow>();return Window.ToSharedRef();}
    void InitializeWindow(const TSharedRef<FGenericWindow>& W,const TSharedRef<FGenericWindowDefinition>& D,const TSharedPtr<FGenericWindow>&,bool Show) override {StaticCastSharedRef<FWebCudaWindow>(W)->Initialize(D);if(Show)W->Show();}
    void GetInitialDisplayMetrics(FDisplayMetrics& M) const override {WebCudaBrowser::Size(M.PrimaryDisplayWidth,M.PrimaryDisplayHeight);M.PrimaryDisplayWorkAreaRect={0,0,M.PrimaryDisplayWidth,M.PrimaryDisplayHeight};M.VirtualDisplayRect=M.PrimaryDisplayWorkAreaRect;}
    void DestroyApplication() override {WebCudaBrowser::Shutdown();}
    void ProcessDeferredEvents(float) override {
        WebCudaBrowser::Event E;
        while(WebCudaBrowser::Poll(E)) {
            using WebCudaBrowser::Kind;
            switch(E.Type) {
            case Kind::Move: StaticCastSharedPtr<FWebCudaCursor>(Cursor)->Position=FVector2D(E.X,E.Y);MessageHandler->OnMouseMove();break;
            case Kind::Down: HeldButtons.Add(E.A);MessageHandler->OnMouseDown(Window,Button(E.A),FVector2D(E.X,E.Y));break;
            case Kind::Up: HeldButtons.Remove(E.A);MessageHandler->OnMouseUp(Button(E.A),FVector2D(E.X,E.Y));break;
            case Kind::Wheel: MessageHandler->OnMouseWheel(E.X);break;
            case Kind::KeyDown: HeldKeys.Add(E.A);MessageHandler->OnKeyDown(E.A,0,E.Repeat);break;
            case Kind::KeyUp: HeldKeys.Remove(E.A);MessageHandler->OnKeyUp(E.A,0,E.Repeat);break;
            case Kind::Character:
                if(E.A>0 && E.A<=0xFFFF)MessageHandler->OnKeyChar(TCHAR(E.A),E.Repeat);
                else if(E.A>0xFFFF && E.A<=0x10FFFF){const uint32 C=E.A-0x10000;MessageHandler->OnKeyChar(TCHAR(0xD800+(C>>10)),E.Repeat);MessageHandler->OnKeyChar(TCHAR(0xDC00+(C&1023)),E.Repeat);}break;
            case Kind::Resize: if(Window)MessageHandler->OnSizeChanged(Window.ToSharedRef(),E.A,E.B);break;
            case Kind::Focus: if(Window)MessageHandler->OnWindowActivationChanged(Window.ToSharedRef(),E.A?EWindowActivation::Activate:EWindowActivation::Deactivate);break;
            case Kind::Reset: for(int32 Key:HeldKeys)MessageHandler->OnKeyUp(Key,0,false);HeldKeys.Empty();for(int32 B:HeldButtons)MessageHandler->OnMouseUp(Button(B));HeldButtons.Empty();break;
            }
        }
    }
};
GenericApplication* FWebCudaPlatformApplicationMisc::CreateApplication()
{
    checkf(WebCudaBrowser::Initialize(),TEXT("WebCuda requires a #canvas element before startup"));
    return new FWebCudaApplication();
}
bool FWebCudaPlatformApplicationMisc::IsThisApplicationForeground(){return WebCudaBrowser::Foreground();}
