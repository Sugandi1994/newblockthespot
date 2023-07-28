﻿#include "pch.h"

/*
* 
* Black banner still show even libcef hooked.
* 
*/

//using _cef_urlrequest_create = cef_urlrequest_t * (*)(struct _cef_request_t* request,
//	struct _cef_urlrequest_client_t* client,
//	struct _cef_request_context_t* request_context);

using _cef_urlrequest_create = void* (*)(void* request, void* client, void* request_context);
static _cef_urlrequest_create cef_urlrequest_create_orig;

using _cef_string_userfree_utf16_free = void * (*)(/*void* str*/void* addr);
static _cef_string_userfree_utf16_free cef_string_userfree_utf16_free_orig;

static constexpr std::array<std::wstring_view, 3> block_list = { L"/ads/", L"/ad-logic/", L"/gabo-receiver-service/" };

#ifdef _WIN64
static std::wstring file_name;
std::uintptr_t file_name_pointer;
std::uintptr_t ret_addr_file_name;
std::uintptr_t ret_addr_file_source;
PatternScanner::ModuleInfo ZipScan;
#else
//static bool xpui_found = false;
static std::wstring file_name;
static std::uintptr_t file_name_pointer;
static std::uintptr_t ret_addr_file_name;
static std::uintptr_t ret_addr_file_source;
static PatternScanner::ModuleInfo ZipScan;

DWORD WINAPI get_url(DWORD pRequest)
{
	DWORD retval = 0;
	__asm
	{
		mov eax, pRequest
		mov ecx, dword ptr ds:[eax+0x18]
		mov edx, eax
		push edx
		call ecx
		mov retval, eax
	}
	return retval;
}

DWORD WINAPI get_str(DWORD pRequest)
{
	DWORD retval = 0;
	__asm
	{
		// get url str
		mov ecx, pRequest
		mov eax, dword ptr ds : [ecx]
		mov retval, eax
	}
	return retval;
}
#endif

#ifndef NDEBUG
void* cef_urlrequest_create_hook (struct _cef_request_t* request, void* client, void* request_context)
#else
void* cef_urlrequest_create_hook(void* request, void* client, void* request_context)
#endif
{
#ifndef NDEBUG
	cef_string_utf16_t* url_utf16 = request->get_url (request);
	std::wstring url(url_utf16->str);
#else

#ifdef _WIN64
	auto get_url = *(std::uintptr_t(__fastcall**)(void*))((std::uintptr_t)request + 48);
	auto url_utf16 = get_url(request);
	std::wstring url(*reinterpret_cast<wchar_t**>(url_utf16));
#else
	auto url_utf16 = get_url(reinterpret_cast<DWORD>(request));
	std::wstring url(reinterpret_cast<wchar_t*>(get_str(url_utf16)));
#endif
	
#endif
	for (const auto& blockurl : block_list) {
		if (std::wstring_view::npos != url.find (blockurl)) {
			Logger::Log(L"blocked - " + url, Logger::LogLevel::Info);
			//cef_string_userfree_utf16_free(url_utf16);
			cef_string_userfree_utf16_free_orig((void*)url_utf16);
			return nullptr;
		}
	}
	//cef_string_userfree_utf16_free(url_utf16);
	cef_string_userfree_utf16_free_orig((void*)url_utf16);
	Logger::Log(L"allow - " + url, Logger::LogLevel::Info);
	return cef_urlrequest_create_orig (request, client, request_context);
}

void WINAPI get_file_name()
{
	try {
		file_name = *reinterpret_cast<wchar_t**>(file_name_pointer);
		//Print(L"{}", file_name);
		//_wsystem(L"pause");
	}
	catch (const std::exception& e) {
		Print({ Color::Red }, L"[{}] {}", L"ERROR", e.what());
	}
}

void WINAPI modify_source()
{
	try {
		if (file_name == L"home-hpto.css")
		{
			//Print(L"{}", zip_file_name);
			const auto hpto = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L".WiPggcPDzbwGxoxwLWFf{-webkit-box-pack:center;-ms-flex-pack:center;display:-webkit-box;display:-ms-flexbox;display:flex;");
			if (hpto.is_found()) {
				if (Memory::Write<const char*>(hpto.data(), ".WiPggcPDzbwGxoxwLWFf{-webkit-box-pack:center;-ms-flex-pack:center;display:-webkit-box;display:-ms-flexbox;display:none;")) {
					Logger::Log(L"hptocss patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"hptocss patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"hptocss - failed not found!", Logger::LogLevel::Error);
			}
		}

		if (file_name == L"xpui-routes-profile.js")
		{
			//Print(L"{}", zip_file_name);
			const auto isModalOpen = PatternScanner::ScanAll(ZipScan.base_address, ZipScan.image_size, L"isModalOpen:!0");
			if (isModalOpen[0].is_found()) {
				for (const auto& it : isModalOpen) {
					if (Memory::Write<const char>(it.offset(13).data(), '1')) {
						Logger::Log(L"isModalOpen patched!", Logger::LogLevel::Info);
					}
					else {
						Logger::Log(L"isModalOpen - patch failed!", Logger::LogLevel::Error);
					}
				}
			}
			else {
				Logger::Log(L"isModalOpen - failed not found!", Logger::LogLevel::Error);
			}
		}

		if (file_name == L"xpui.js")
		{
			//Print(L"{}", zip_file_name);
			const auto skipads = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"adsEnabled:!0");
			if (skipads.is_found()) {
				if (Memory::Write<const char>(skipads.offset(12).data(), '1')) {
					Logger::Log(L"adsEnabled patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"adsEnabled - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"adsEnabled - failed not found!", Logger::LogLevel::Error);
			}

			const auto sponsorship = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L".set(\"allSponsorships\",t.sponsorships)}}(e,t);");
			if (sponsorship.is_found())
			{
				memset((char*)sponsorship.data() + 6, 0x22, 1);
				memset((char*)sponsorship.data() + 7, 0x20, 15);
				Logger::Log(L"sponsorship patched!", Logger::LogLevel::Info);
				//if (Memory::Write<char>(sponsorship.offset(6).data(), ' ', 15)) {
				//	Logger::Log(L"sponsorship patched!", Logger::LogLevel::Info);
				//}
				//else {
				//	Logger::Log(L"sponsorship patch failed!", Logger::LogLevel::Error);
				//}
			}
			else {
				Logger::Log(L"sponsorship - failed not found!", Logger::LogLevel::Error);
			}

			const auto skipsentry = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"sentry.io");
			if (skipsentry.is_found()) {
				if (Memory::Write<const char*>(skipsentry.data(), "localhost")) {
					Logger::Log(L"sentry.io -> localhost patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"sentry.io -> localhost - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"sentry.io -> localhost - failed not found!", Logger::LogLevel::Error);
			}

			const auto ishptoenable = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"hptoEnabled:!0");
			if (ishptoenable.is_found())
			{
				if (Memory::Write<const char>(ishptoenable.offset(13).data(), '1')) {
					Logger::Log(L"hptoEnabled patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"hptoEnabled - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"hptoEnabled - failed not found!", Logger::LogLevel::Error);
			}

			const auto ishptohidden = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"isHptoHidden:!0");
			if (ishptohidden.is_found()) {
				if (Memory::Write<const char>(ishptohidden.offset(14).data(), '1')) {
					Logger::Log(L"isHptoHidden patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"isHptoHidden - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"isHptoHidden - failed not found!", Logger::LogLevel::Error);
			}

			const auto sp_localhost = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"sp://ads/v1/ads/");
			if (sp_localhost.is_found()) {
				if (Memory::Write<const char*>(sp_localhost.data(), "sp://localhost//")) {
					Logger::Log(L"sp://ads/v1/ads/ patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"sp://ads/v1/ads/ - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"sp://ads/v1/ads/ - failed not found!", Logger::LogLevel::Error);
			}

			const auto premium_free = PatternScanner::ScanFirst(ZipScan.base_address, ZipScan.image_size, L"\"free\"===e.session?.productState?.catalogue?.toLowerCase(),r=e=>null!==e.session?.productState&&1===parseInt(e.session?.productState?.ads,10),o=e=>\"premium\"===e.session?.productState?.catalogue?.toLowerCase(),");
			if (premium_free.is_found()) {
				//Print(L"{}", premium_free.read<const char*>());
				//_wsystem(L"pause");
				if (Memory::Write<const char*>(premium_free.data(), "\"premium\"===e.session?.productState?.catalogue?.toLowerCase(),r=e=>null!==e.session?.productState&&1===parseInt(e.session?.productState?.ads,10),o=e=>\"free\"===e.session?.productState?.catalogue?.toLowerCase(),")) {
					Logger::Log(L"premium patched!", Logger::LogLevel::Info);
				}
				else {
					Logger::Log(L"premium - patch failed!", Logger::LogLevel::Error);
				}
			}
			else {
				Logger::Log(L"premium - failed not found!", Logger::LogLevel::Error);
			}
		}
	}
	catch (const std::exception& e) {
		Print({ Color::Red }, L"[{}] {}", L"ERROR", e.what());
	}
}

#ifdef _WIN64

extern "C" void hook_file_name();
extern "C" void hook_zip_buffer();

#else
__declspec(naked) void hook_file_name()
{
	__asm
	{
		mov dword ptr ss : [ebp - 0x18] , ebx
		mov byte ptr ss : [ebp - 0x14] , bl
		push eax

		mov file_name_pointer, eax

		pushad
		call get_file_name
		popad
		push ret_addr_file_name
		retn
	}
}

__declspec(naked) void hook_zip_buffer()
{
	__asm
	{
		add eax, edi
		mov ZipScan.base_address, eax
		push eax
		call edx
		mov ZipScan.image_size, eax
		//cmp xpui_found, 0
		//jne skip

		//------------ preparation --------------------
		pushad

		//------------ function call ------------------
		call modify_source

		//------------ end call ------------------
		popad

		//------------ finish -------------------------
	//skip:
	push ret_addr_file_source
	retn
	}
}
#endif

DWORD WINAPI EnableDeveloper(LPVOID lpParam)
{
	try
	{
#ifdef _WIN64
		const auto developer = PatternScanner::ScanFirst(L"41 22 DE 48 8B 95 40 05 00 00");
		if (Memory::Write<std::vector<uint8_t>>(developer.data(), { 0xB3, 0x03, 0x90 })) {
			Logger::Log(L"Developer - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"Developer - patch failed!", Logger::LogLevel::Error);
		}
#else
		const auto developer = PatternScanner::ScanFirst(L"25 01 FF FF FF 89 ?? ?? ?? FF FF");
		if (Memory::Write<std::vector<uint8_t>>(developer.data(), { 0xB8, 0x03, 0x00 })) {
			Logger::Log(L"Developer - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"Developer - patch failed!", Logger::LogLevel::Error);
		}
#endif
	}
	catch (const std::exception& e)
	{
		Print({ Color::Red }, L"[{}] {}", L"ERROR", e.what());
	}
	return 0;
}

DWORD WINAPI BlockAds(LPVOID lpParam)
{
	try
	{
#if 0
		const auto pod = PatternScanner::ScanFirst(L"80 7C 24 70 07 0F 85 ?? ?? ?? ?? 48 8D").offset(5);
		if (Memory::Write<std::vector<uint8_t>>(pod.data(), { 0x90, 0xE9 })) {
			Logger::Log(L"Block Audio Ads - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"Block Audio Ads - patch failed!", Logger::LogLevel::Error);
		}
#else
		auto hModule = GetModuleHandleW(L"libcef.dll");
		if (!hModule)
			hModule = LoadLibraryW(L"libcef.dll");

		if (hModule) {
			cef_urlrequest_create_orig = /*cef_urlrequest_create;*/(_cef_urlrequest_create)GetProcAddress(hModule, "cef_urlrequest_create");
			cef_string_userfree_utf16_free_orig = /*cef_urlrequest_create;*/(_cef_string_userfree_utf16_free)GetProcAddress(hModule, "cef_string_userfree_utf16_free");

			if (cef_urlrequest_create_orig && cef_string_userfree_utf16_free_orig) {
				if (!Hooking::HookFunction(&(PVOID&)cef_urlrequest_create_orig, (PVOID)cef_urlrequest_create_hook)) {
					Logger::Log(L"BlockAds - patch failed!", Logger::LogLevel::Error);
				}
				else {
					Logger::Log(L"BlockAds - patch success!", Logger::LogLevel::Info);
				}
			}
		}
#endif
	}
	catch (const std::exception& e)
	{
		Print({ Color::Red }, L"[{}] {}", L"ERROR", e.what());
	}
	return 0;
}

DWORD WINAPI BlockBanner(LPVOID lpParam)
{
	try
	{
#ifdef _WIN64
		const auto FileName = PatternScanner::ScanFirst(L"48 85 C9 74 23 38 5C 24 48 74 14 E8 ?? ?? ?? ?? BA 18 00 00 00 48 8B 4C 24 40 E8 ?? ?? ?? ?? 48 89 5C 24 40 88 5C 24 48 48 8D 5E 08");
		ret_addr_file_name = FileName + 5;
		if (FileName.hook((PVOID)hook_file_name)) {
			Logger::Log(L"FileName - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"FileName - patch failed!", Logger::LogLevel::Error);
		}

		const auto SourceCode = PatternScanner::ScanFirst(L"48 63 C8 48 03 F1 49 3B F4 73 25 41 F6 C6 04");
		ret_addr_file_source = SourceCode + 6;
		if (SourceCode.hook((PVOID)hook_zip_buffer)) {
			Logger::Log(L"SourceCode - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"SourceCode - patch failed!", Logger::LogLevel::Error);
		}
#else
		const auto FileName = PatternScanner::ScanFirst(L"8B 48 24 85 C9 74 4A 50 FF D1 C7 45 ?? ?? ?? ?? ?? 89 5D E8 88 5D EC 50").offset(0x11);
		ret_addr_file_name = FileName + 7;

		if (FileName.hook((PVOID)hook_file_name)) {
			Logger::Log(L"FileName - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"FileName - patch failed!", Logger::LogLevel::Error);
		}

		const auto SourceCode = PatternScanner::ScanFirst(L"8B 45 EC 03 C7 50 FF D2 03 F8").offset(3);
		ret_addr_file_source = SourceCode + 5;
		if (SourceCode.hook(hook_zip_buffer)) {
			Logger::Log(L"SourceCode - patch success!", Logger::LogLevel::Info);
		}
		else {
			Logger::Log(L"SourceCode - patch failed!", Logger::LogLevel::Error);
		}
#endif
	}
	catch (const std::exception& e)
	{
		Print({ Color::Red }, L"[{}] {}", L"ERROR", e.what());
	}
	return 0;
}