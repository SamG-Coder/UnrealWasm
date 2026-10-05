#include <sched.h>
namespace UE::Trace::Private {
// Uses the explicit platform hook instead of x86/ARM assembly in Trace's header.
void ThreadYield() { sched_yield(); }
}
