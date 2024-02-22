#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <Windows.h>
#include <array>
#include <string_view>
#include <map>

#include "BasicUtils/Utils.h"
#include "BasicUtils/Logger.h"
#include "BasicUtils/MemoryScanner.h"
#include "BasicUtils/Memory.h"
#include "BasicUtils/Hooking.h"

#ifndef NDEBUG
#include <include/capi/cef_urlrequest_capi.h>
#include <include/capi/cef_zip_reader_capi.h>
#endif