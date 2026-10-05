#pragma once
#include "GenericPlatform/GenericPlatformSplash.h"

// Browser startup has no separate native splash window. Use Unreal's existing
// optional-splash implementation; canvas/window/input integration is separate.
using FPlatformSplash = FGenericPlatformSplash;
