#include "WebCuda/WebCudaHttpTransport.h"
#include <emscripten.h>
#include <map>
#include <string>

namespace { std::map<uint32_t,WebCudaHttp::Result> Results; std::vector<uint8_t> HeapGrowth; }
extern "C" {
EMSCRIPTEN_KEEPALIVE uint32_t http_start(int Case)
{
    WebCudaHttp::Request Request{"/echo"};
    uint8_t Body[8192]; for(unsigned I=0;I<sizeof(Body);++I)Body[I]=uint8_t(I*37+11);
    switch(Case) {
    case 0:Request.Verb="POST";Request.Body=Body;Request.BodyBytes=sizeof(Body);Request.Headers="content-type: application/octet-stream\nx-case: binary";break;
    case 1:Request.Url="/missing";break;
    case 2:Request.Url="/redirect";break;
    case 3:Request.Url="/slow";Request.TimeoutSeconds=0.05;break;
    case 4:Request.Url="/slow";break;
    case 5:Request.Url="/stream";break;
    case 6:Request.Url="/large";Request.MaxResponseBytes=16;break;
    case 7:Request.Url="/drop";break;
    case 8:Request.Url="/slow";Request.ActivityTimeoutSeconds=0.05;break;
    case 9:Request.Url="file:///unavailable";break;
    case 10:Request.Url="/echo";Request.Headers="bad header: value";break;
    case 11:Request.Url="/utf8";break;
    case 12:Request.Url="/head";Request.Verb="HEAD";break;
    default:return 0;
    }
    return WebCudaHttp::Start(Request);
}
EMSCRIPTEN_KEEPALIVE int http_poll(uint32_t Job) {
    if(Results.count(Job)) return 1;
    WebCudaHttp::Result Result;
    if(!WebCudaHttp::TakeResult(Job,Result))return 0;
    Results.emplace(Job,std::move(Result));return 1;
}
EMSCRIPTEN_KEEPALIVE int http_error(uint32_t Job) {return int(Results.at(Job).Failure);}
EMSCRIPTEN_KEEPALIVE int http_status(uint32_t Job) {return Results.at(Job).StatusCode;}
EMSCRIPTEN_KEEPALIVE int http_size(uint32_t Job) {return int(Results.at(Job).Body.size());}
EMSCRIPTEN_KEEPALIVE const char* http_url(uint32_t Job) {return Results.at(Job).EffectiveUrl.c_str();}
EMSCRIPTEN_KEEPALIVE const char* http_headers(uint32_t Job) {return Results.at(Job).Headers.c_str();}
EMSCRIPTEN_KEEPALIVE const uint8_t* http_body(uint32_t Job) {return Results.at(Job).Body.data();}
EMSCRIPTEN_KEEPALIVE int http_progress(uint32_t Job) {WebCudaHttp::Progress P{};return WebCudaHttp::GetProgress(Job,P)?int(P.Received):-1;}
EMSCRIPTEN_KEEPALIVE void http_cancel(uint32_t Job) {WebCudaHttp::Cancel(Job);}
EMSCRIPTEN_KEEPALIVE void http_forget(uint32_t Job) {WebCudaHttp::Forget(Job);Results.erase(Job);}
EMSCRIPTEN_KEEPALIVE uint32_t http_active() {return WebCudaHttp::ActiveJobs();}
EMSCRIPTEN_KEEPALIVE int http_grow() {HeapGrowth.resize(64*1024*1024,37);return HeapGrowth.back();}
}
