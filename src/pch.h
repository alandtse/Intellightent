#pragma once

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <Windows.h>
#include <chrono>
#include <corecrt_math_defines.h>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <tlhelp32.h>
#include <unordered_map>
#include <vector>

#include "MemoryHelper.h"
#include "formula.h"

namespace logs = SKSE::log;
using namespace std::literals;
