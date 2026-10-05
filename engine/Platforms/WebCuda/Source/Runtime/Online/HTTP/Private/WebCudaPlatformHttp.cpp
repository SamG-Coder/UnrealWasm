#include "WebCuda/WebCudaPlatformHttp.h"
#include "WebCuda/WebCudaHttpTransport.h"
#include "GenericPlatform/HttpRequestImpl.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Containers/StringConv.h"

namespace
{
using FHeaders = TMap<FString,FString>;
FString HeaderValue(const FHeaders& Headers,const FString& Key)
{
    const FString* Value=Headers.Find(Key.ToLower()); return Value?*Value:FString();
}
TArray<FString> HeaderLines(const FHeaders& Headers)
{
    TArray<FString> Lines; for(const auto& Pair:Headers) Lines.Add(Pair.Key+TEXT(": ")+Pair.Value); return Lines;
}
EHttpFailureReason FailureReason(WebCudaHttp::Error Error)
{
    switch(Error) {
    case WebCudaHttp::Error::None:return EHttpFailureReason::None;
    case WebCudaHttp::Error::Connection:return EHttpFailureReason::ConnectionError;
    case WebCudaHttp::Error::Cancelled:return EHttpFailureReason::Cancelled;
    case WebCudaHttp::Error::TimedOut:return EHttpFailureReason::TimedOut;
    case WebCudaHttp::Error::TooLarge:return EHttpFailureReason::ResponseTooLarge;
    default:return EHttpFailureReason::Other;
    }
}
class FWebCudaResponse final : public IHttpResponse
{
public:
    FString Url,EffectiveUrl;
    FHeaders Headers;
    TArray<uint8> Body;
    int32 Code=0;
    EHttpFailureReason Failure=EHttpFailureReason::None;
    const FString& GetURL() const override {return Url;}
    const FString& GetEffectiveURL() const override {return EffectiveUrl;}
    EHttpRequestStatus::Type GetStatus() const override {return Failure==EHttpFailureReason::None?EHttpRequestStatus::Succeeded:EHttpRequestStatus::Failed;}
    EHttpFailureReason GetFailureReason() const override {return Failure;}
    FString GetURLParameter(const FString& Name) const override {return FGenericPlatformHttp::GetUrlParameter(Url,Name).Get(FString());}
    FString GetHeader(const FString& Name) const override {return HeaderValue(Headers,Name);}
    TArray<FString> GetAllHeaders() const override {return HeaderLines(Headers);}
    FString GetContentType() const override {return GetHeader(TEXT("content-type"));}
    uint64 GetContentLength() const override {return FCString::Strtoui64(*GetHeader(TEXT("content-length")),nullptr,10);}
    const TArray<uint8>& GetContent() const override {return Body;}
    TArray<uint8> TakeContent() override {return MoveTemp(Body);}
    int32 GetResponseCode() const override {return Code;}
    FString GetContentAsString() const override {
        if(Body.IsEmpty()) return FString();
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Body.GetData()),Body.Num());
        return FString(Converted.Length(),Converted.Get());
    }
    FUtf8StringView GetContentAsUtf8StringView() const override {return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Body.GetData()),Body.Num());}
};

class FWebCudaRequest final : public FHttpRequestImpl
{
    FString Url,EffectiveUrl,Verb=TEXT("GET");
    FHeaders Headers;
    TMap<FName,FString> Options;
    TArray<uint8> Body;
    TSharedPtr<FArchive> ReceiveStream;
    TOptional<float> Timeout,ActivityTimeout;
    FHttpResponsePtr Response;
    EHttpRequestStatus::Type Status=EHttpRequestStatus::NotStarted;
    EHttpFailureReason Failure=EHttpFailureReason::None;
    EHttpRequestDelegateThreadPolicy Policy=EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
    EHttpRequestPriority Priority=EHttpRequestPriority::Normal;
    uint32 Job=0,Received=0;
    double StartTime=0,Elapsed=0;
    bool CompletionQueued=false;
    bool Cancelled=false,DispatchingMetadata=false;
    bool Busy() const {return Status==EHttpRequestStatus::Processing;}

    void Finish(WebCudaHttp::Result Result)
    {
        if(!Busy()) return;
        auto Self=AsShared();
        if(Cancelled) Result.Failure=WebCudaHttp::Error::Cancelled;
        Elapsed=FPlatformTime::Seconds()-StartTime;
        Failure=FailureReason(Result.Failure);
        EffectiveUrl=UTF8_TO_TCHAR(Result.EffectiveUrl.c_str());
        if(Result.StatusCode) {
            auto NewResponse=MakeShared<FWebCudaResponse,ESPMode::ThreadSafe>();
            NewResponse->Url=Url; NewResponse->EffectiveUrl=EffectiveUrl; NewResponse->Code=Result.StatusCode;
            TArray<FString> Lines; FString(UTF8_TO_TCHAR(Result.Headers.c_str())).ParseIntoArrayLines(Lines);
            for(const FString& Line:Lines) {
                FString Key,Value; if(Line.Split(TEXT(":"),&Key,&Value)) NewResponse->Headers.Add(Key.ToLower(),Value.TrimStartAndEnd());
            }
            if(ReceiveStream && Failure==EHttpFailureReason::None) {
                ReceiveStream->Serialize(Result.Body.data(),int64(Result.Body.size()));
                if(ReceiveStream->IsError()) Failure=EHttpFailureReason::Other;
            } else if(!Result.Body.empty()) NewResponse->Body.Append(Result.Body.data(),int32(Result.Body.size()));
            NewResponse->Failure=Failure; Response=NewResponse;
        }
        Status=Failure==EHttpFailureReason::None?EHttpRequestStatus::Succeeded:EHttpRequestStatus::Failed;
        FHttpManager& Manager=FHttpModule::Get().GetHttpManager();
        Manager.RemoveRequest(Self);
        // Capture the terminal result before calling delegates: a completion
        // callback may reuse the request immediately, starting a different job.
        const FHttpResponsePtr CompletedResponse=Response;
        const bool Succeeded=Failure==EHttpFailureReason::None;
        Manager.MarkCurrentThreadCompletingRequest(true);
        DispatchingMetadata=true;
        if(CompletedResponse) {
            StatusCodeReceivedDelegate.ExecuteIfBound(Self,CompletedResponse->GetResponseCode());
            for(const FString& Line:CompletedResponse->GetAllHeaders()) {
                FString Key,Value; if(Line.Split(TEXT(":"),&Key,&Value)) HeaderReceivedDelegate.ExecuteIfBound(Self,Key,Value.TrimStartAndEnd());
            }
        }
        Manager.BroadcastHttpRequestCompleted(Self);
        DispatchingMetadata=false;
        RequestCompleteDelegate.ExecuteIfBound(Self,CompletedResponse,Succeeded);
        Manager.MarkCurrentThreadCompletingRequest(false);
    }
public:
    ~FWebCudaRequest() override {if(Job) WebCudaHttp::Forget(Job);}
    const FString& GetURL() const override {return Url;}
    const FString& GetEffectiveURL() const override {return EffectiveUrl.IsEmpty()?Url:EffectiveUrl;}
    EHttpRequestStatus::Type GetStatus() const override {return Status;}
    EHttpFailureReason GetFailureReason() const override {return Failure;}
    FString GetURLParameter(const FString& Name) const override {return FGenericPlatformHttp::GetUrlParameter(Url,Name).Get(FString());}
    FString GetHeader(const FString& Name) const override {return HeaderValue(Headers,Name);}
    TArray<FString> GetAllHeaders() const override {return HeaderLines(Headers);}
    FString GetContentType() const override {return GetHeader(TEXT("content-type"));}
    uint64 GetContentLength() const override {return uint64(Body.Num());}
    const TArray<uint8>& GetContent() const override {return Body;}
    FString GetVerb() const override {return Verb;}
    void SetVerb(const FString& Value) override {if(!Busy()) Verb=Value.ToUpper();}
    void SetURL(const FString& Value) override {if(!Busy()) Url=Value;}
    FString GetOption(FName Name) const override {const FString* Value=Options.Find(Name);return Value?*Value:FString();}
    void SetOption(FName Name,const FString& Value) override {if(!Busy()) Options.Add(Name,Value);}
    void SetContent(const TArray<uint8>& Value) override {if(!Busy()) Body=Value;}
    void SetContent(TArray<uint8>&& Value) override {if(!Busy()) Body=MoveTemp(Value);}
    void SetContentAsString(const FString& Value) override {
        if(Busy()) return; FTCHARToUTF8 Utf8(*Value); Body.Reset(); Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    }
    bool SetContentAsStreamedFile(const FString&) override {return false;} // Streaming upload is not supported by this transport.
    bool SetContentFromStream(TSharedRef<FArchive,ESPMode::ThreadSafe>) override {return false;}
    bool SetResponseBodyReceiveStream(TSharedRef<FArchive> Stream) override {if(Busy())return false;ReceiveStream=Stream;return true;}
    void SetHeader(const FString& Name,const FString& Value) override {if(!Busy())Headers.Add(Name.ToLower(),Value);}
    void AppendToHeader(const FString& Name,const FString& Value) override {
        if(Busy())return; FString Old=GetHeader(Name);SetHeader(Name,Old.IsEmpty()?Value:Old+TEXT(", ")+Value);
    }
    void SetTimeout(float Value) override {if(!Busy())Timeout=Value;}
    void SetActivityTimeout(float Value) override {if(!Busy())ActivityTimeout=Value;}
    void ClearTimeout() override {if(!Busy())Timeout.Reset();}
    void ResetTimeoutStatus() override {if(!Busy()){Elapsed=0;Failure=EHttpFailureReason::None;}}
    TOptional<float> GetTimeout() const override {return Timeout;}
    bool ProcessRequest() override {
        check(IsInGameThread());
        if(Busy() || DispatchingMetadata) return false;
        Status=EHttpRequestStatus::Processing; Failure=EHttpFailureReason::None; Response.Reset(); EffectiveUrl.Reset();
        Received=0; CompletionQueued=false; Cancelled=false; StartTime=FPlatformTime::Seconds();
        FHttpModule& Module=FHttpModule::Get(); Module.GetHttpManager().AddRequest(AsShared());
        for(const auto& Header:Module.GetDefaultHeaders()) if(!Headers.Contains(Header.Key.ToLower())) Headers.Add(Header.Key.ToLower(),Header.Value);
        bool Supported=!Cancelled && Module.IsHttpEnabled() && Module.GetHttpManager().IsDomainAllowed(Url) && Policy==EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
        for(const auto& Option:Options) if(Option.Key!=HttpRequestOptions::EnableCookies && Option.Key!=HttpRequestOptions::RequestMode)Supported=false;
        for(const auto& Header:Headers) if(Header.Key.Contains(TEXT(":")) || Header.Key.Contains(TEXT("\n")) || Header.Key.Contains(TEXT("\r")) || Header.Value.Contains(TEXT("\n")) || Header.Value.Contains(TEXT("\r")))Supported=false;
        FTCHARToUTF8 Utf8Url(*Url),Utf8Verb(*Verb),Utf8Headers(*FString::Join(GetAllHeaders(),TEXT("\n")));
        WebCudaHttp::Request Input{Utf8Url.Get(),Utf8Verb.Get(),Utf8Headers.Get(),Body.GetData(),uint32(Body.Num())};
        Input.TimeoutSeconds=Timeout.Get(Module.GetHttpTotalTimeout());
        Input.ActivityTimeoutSeconds=ActivityTimeout.Get(Module.GetHttpActivityTimeout());
        const FString Cookies=GetOption(HttpRequestOptions::EnableCookies);
        Input.Credentials=Cookies.IsEmpty()?0:Cookies.ToBool()?1:2;
        Job=Supported?WebCudaHttp::Start(Input):0;
        return Job!=0;
    }
    void CancelRequest() override {if(Busy())Cancelled=true;if(Job)WebCudaHttp::Cancel(Job);}
    const FHttpResponsePtr GetResponse() const override {return Response;}
    void Tick(float) override {
        if(!Busy() || CompletionQueued)return;
        WebCudaHttp::Progress Progress{};
        if(Job && WebCudaHttp::GetProgress(Job,Progress)) {
            if(Progress.Received!=Received) {Received=Progress.Received;RequestProgressDelegate64.ExecuteIfBound(AsShared(),uint64(Body.Num()),uint64(Received));}
            if(!Progress.Done)return;
        }
        WebCudaHttp::Result Result;
        if(!Job || !WebCudaHttp::TakeResult(Job,Result))Result.Failure=WebCudaHttp::Error::Other;
        Job=0; CompletionQueued=true;
        auto Self=StaticCastSharedRef<FWebCudaRequest>(AsShared());
        // Defer removal until after FHttpManager's active-request iteration.
        FHttpModule::Get().GetHttpManager().AddGameThreadTask([Self,Result=MoveTemp(Result)]() mutable {Self->Finish(MoveTemp(Result));});
    }
    float GetElapsedTime() const override {return float(Busy()?FPlatformTime::Seconds()-StartTime:Elapsed);}
    void SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy Value) override {if(!Busy())Policy=Value;}
    EHttpRequestDelegateThreadPolicy GetDelegateThreadPolicy() const override {return Policy;}
    void SetPriority(EHttpRequestPriority Value) override {if(!Busy())Priority=Value;}
    EHttpRequestPriority GetPriority() const override {return Priority;}
    void ProcessRequestUntilComplete() override {
        // Fetch cannot progress while the browser thread is synchronously blocked.
        // Return an explicit failed completion rather than blocking its event loop.
        if(Busy())return;
        Status=EHttpRequestStatus::Failed;Failure=EHttpFailureReason::Other;Response.Reset();
        RequestCompleteDelegate.ExecuteIfBound(AsShared(),nullptr,false);
    }
};
class FWebCudaHttpManager final : public FHttpManager
{
public:
    void FlushTick(float Delta) override {
        // Synchronous flushing must cancel: waiting would prevent Fetch from
        // making progress. Tick twice to drain deferred terminal completions.
        const TArray<FHttpRequestRef> Snapshot=Requests;
        for(const auto& Request:Snapshot) Request->CancelRequest();
        FHttpManager::Tick(Delta); FHttpManager::Tick(0);
    }
};
}
IHttpRequest* FWebCudaPlatformHttp::ConstructRequest() {return new FWebCudaRequest();}
FHttpManager* FWebCudaPlatformHttp::CreatePlatformHttpManager() {return new FWebCudaHttpManager();}
