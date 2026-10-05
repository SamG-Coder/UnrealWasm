#include "WebCuda/WebCudaHttpTransport.h"
#include <emscripten.h>
#include <emscripten/em_macros.h>

EM_JS_DEPS(webcuda_http_strings, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

namespace WebCudaHttp
{
namespace
{
EM_JS(uint32_t, WebCudaHttpBegin, (const char* url, const char* verb, const char* headers,
    const void* body, uint32_t bytes, uint32_t maximum, double timeout, double activity, int credentials), {
    const state = Module['webcudaHttp'] ||= {next:1, jobs:new Map()};
    if(state.next >= 0x7fffffff) return 0;
    const id = state.next++;
    const job = {done:false, error:0, code:0, received:0, chunks:[], headers:"", url:"",
        controller:new AbortController(), timer:0, idle:0};
    state.jobs.set(id, job);
    job.finish = (error) => {
        if(job.done) return;
        job.done=true; job.error=error;
        clearTimeout(job.timer); clearTimeout(job.idle);
    };
    job.stop = (error) => { job.finish(error); job.controller.abort(); };
    const refresh = () => {
        clearTimeout(job.idle);
        if(activity > 0 && !job.done) job.idle=setTimeout(()=>job.stop(3), activity*1000);
    };
    if(timeout > 0) job.timer=setTimeout(()=>job.stop(3), timeout*1000);
    refresh();
    // Copy input before returning to C++; UE may release or replace its buffers.
    let options, target;
    try {
        target=new URL(UTF8ToString(Number(url)), location.href);
        if(!['http:','https:'].includes(target.protocol)) throw Error('Unsupported URL protocol');
        const method=UTF8ToString(Number(verb));
        const fields=new Headers();
        for(const line of UTF8ToString(Number(headers)).split('\n')) {
            if(!line) continue;
            const separator=line.indexOf(':');
            if(separator <= 0) throw Error('Invalid HTTP header');
            fields.append(line.slice(0,separator), line.slice(separator+1).trim());
        }
        options={method, headers:fields, signal:job.controller.signal,
            credentials:credentials===1?'include':credentials===2?'omit':'same-origin', redirect:'follow'};
        if(bytes) options.body=HEAPU8.slice(Number(body), Number(body)+bytes);
    } catch(error) { job.finish(4); return id; }
    (async()=>{
        try {
            const response=await fetch(target, options);
            if(job.done) return;
            job.code=response.status; job.url=response.url;
            response.headers.forEach((value,name)=>{job.headers += name+': '+value+'\n';});
            refresh();
            // Limit decoded bytes as read, not Content-Length (which can describe
            // compressed data). No arrayBuffer() allocation before this check.
            if(response.body) {
                const reader=response.body.getReader();
                try {
                    while(!job.done) {
                        const part=await reader.read();
                        if(job.done) break;
                        if(part.done) break;
                        if(part.value.length > maximum-job.received) {job.stop(5); break;}
                        job.chunks.push(part.value); job.received+=part.value.length; refresh();
                    }
                    if(job.error) await reader.cancel().catch(()=>{});
                } finally {reader.releaseLock();}
            }
            job.finish(0);
        } catch(error) { job.finish(1); }
    })();
    return id;
});
EM_JS(int, WebCudaHttpQuery, (uint32_t id, int field), {
    const job=Module['webcudaHttp']?.jobs.get(id);
    if(!job) return -1;
    switch(field) {
        case 0:return job.done?1:0;
        case 1:return job.error;
        case 2:return job.received;
        case 3:return job.code;
        case 4:return lengthBytesUTF8(job.url);
        case 5:return lengthBytesUTF8(job.headers);
    }
    return -1;
});
EM_JS(void, WebCudaHttpCopy, (uint32_t id, char* url, uint32_t urlBytes, char* headers, uint32_t headerBytes, void* body), {
    const job=Module['webcudaHttp'].jobs.get(id);
    stringToUTF8(job.url, Number(url), urlBytes);
    stringToUTF8(job.headers, Number(headers), headerBytes);
    let offset=Number(body);
    for(const part of job.chunks) {HEAPU8.set(part,offset); offset+=part.length;}
});
EM_JS(void, WebCudaHttpStop, (uint32_t id, bool forget), {
    const state=Module['webcudaHttp']; const job=state?.jobs.get(id);
    if(!job) return;
    if(!job.done) job.stop(2);
    if(forget) {state.jobs.delete(id); job.chunks=[];}
});
EM_JS(uint32_t, WebCudaHttpCount, (), {return Module['webcudaHttp']?.jobs.size || 0;});
}
uint32_t Start(const Request& Input)
{
    if(!Input.Url || !Input.Verb || !Input.Headers || (Input.BodyBytes && !Input.Body) ||
       Input.MaxResponseBytes > 0x7fffffffU) return 0;
    return WebCudaHttpBegin(Input.Url, Input.Verb, Input.Headers, Input.Body, Input.BodyBytes,
        Input.MaxResponseBytes, Input.TimeoutSeconds, Input.ActivityTimeoutSeconds, Input.Credentials);
}
bool GetProgress(uint32_t Job, Progress& Output)
{
    const int Done=WebCudaHttpQuery(Job,0);
    if(Done<0) return false;
    Output={Done!=0, Error(WebCudaHttpQuery(Job,1)), uint32_t(WebCudaHttpQuery(Job,2)), WebCudaHttpQuery(Job,3)};
    return true;
}
bool TakeResult(uint32_t Job, Result& Output)
{
    Progress State{};
    if(!GetProgress(Job,State) || !State.Done) return false;
    Output.Failure=State.Failure; Output.StatusCode=State.StatusCode;
    const uint32_t UrlBytes=uint32_t(WebCudaHttpQuery(Job,4))+1, HeaderBytes=uint32_t(WebCudaHttpQuery(Job,5))+1;
    Output.EffectiveUrl.resize(UrlBytes); Output.Headers.resize(HeaderBytes);
    Output.Body.resize(State.Received);
    WebCudaHttpCopy(Job,Output.EffectiveUrl.data(),UrlBytes,Output.Headers.data(),HeaderBytes,Output.Body.data());
    Output.EffectiveUrl.resize(UrlBytes-1); Output.Headers.resize(HeaderBytes-1);
    Forget(Job);
    return true;
}
void Cancel(uint32_t Job) {WebCudaHttpStop(Job,false);}
void Forget(uint32_t Job) {WebCudaHttpStop(Job,true);}
uint32_t ActiveJobs() {return WebCudaHttpCount();}
}
