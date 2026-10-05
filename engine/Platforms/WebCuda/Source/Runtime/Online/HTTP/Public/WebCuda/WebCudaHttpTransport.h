#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Single browser-thread transport. No Unreal objects cross the JavaScript ABI.
// TakeResult consumes a completed job; Forget aborts and releases any job.
namespace WebCudaHttp
{
enum class Error : int { None, Connection, Cancelled, TimedOut, Other, TooLarge };
struct Request
{
    const char* Url;
    const char* Verb = "GET";
    const char* Headers = "";
    const void* Body = nullptr;
    uint32_t BodyBytes = 0;
    uint32_t MaxResponseBytes = 64 * 1024 * 1024;
    double TimeoutSeconds = 0;
    double ActivityTimeoutSeconds = 0;
    int Credentials = 0; // 0=same-origin, 1=include, 2=omit
};
struct Progress { bool Done; Error Failure; uint32_t Received; int StatusCode; };
struct Result
{
    Error Failure = Error::None;
    int StatusCode = 0;
    std::string EffectiveUrl;
    std::string Headers;
    std::vector<uint8_t> Body;
};
uint32_t Start(const Request& Input);
bool GetProgress(uint32_t Job, Progress& Output);
bool TakeResult(uint32_t Job, Result& Output);
void Cancel(uint32_t Job);
void Forget(uint32_t Job);
uint32_t ActiveJobs();
}
