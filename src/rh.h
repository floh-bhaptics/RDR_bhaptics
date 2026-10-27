#pragma once
// The RedHook SDK's typed natives, without the example's pch (which pulls in its NoClip / InputsManager sources).

#include <windows.h>

// The SDK's headers don't include what they use (Structs.h: std::lerp, sqrt...); its example's pch brought these in
// first (<iostream>, <vector>, <string>), so we do the same explicitly.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include <RedHook.h>

#include "Headers/NativesCaller.h"
#include "Headers/Math.h"
#include "Headers/Enums.h"
#include "Headers/Structs.h"
#include "Headers/Natives.h"
