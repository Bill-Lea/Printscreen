#pragma once

// SKSE first (it has its own windows includes)
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// Standard library
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Then Windows API (after SKSE so it doesn't conflict)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj_core.h>
#include <combaseapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

// DirectXTex
#include <DirectXTex.h>

// Helpers for SKSE plugin
using namespace std::literals;
using namespace REL::literals;