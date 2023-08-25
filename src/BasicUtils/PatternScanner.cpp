﻿#include "PatternScanner.h"
#include "Memory.h"
#include "Console.h"
#include "Hooking.h"
#include "Utils.h"
#include <Psapi.h>
#pragma warning(disable: 4530)
#include <sstream>
#pragma warning(default: 4530)
#include <iomanip>
#include <regex>

PatternScanner::ModuleInfo PatternScanner::GetModuleInfo(std::wstring_view module_name)
{
    const HMODULE module_handle = GetModuleHandleW(module_name.empty() ? nullptr : module_name.data());
    if (!module_handle) {
        PrintError(L"GetModuleInfo: Could not get module handle for {}. Error code: {}", module_name.empty() ? L"main module" : module_name, GetLastError());
        return ModuleInfo();
    }

    MODULEINFO module_info;
    if (!GetModuleInformation(GetCurrentProcess(), module_handle, &module_info, sizeof(module_info))) {
        PrintError(L"GetModuleInfo: Could not get module information for {}. Error code: {}", module_name.empty() ? L"main module" : module_name, GetLastError());
        return ModuleInfo();
    }

    return { reinterpret_cast<std::size_t>(module_info.lpBaseOfDll), static_cast<std::size_t>(module_info.SizeOfImage) };
}

Scan PatternScanner::GetFunctionAddress(std::wstring_view module_name, std::wstring_view function_name)
{
    HMODULE module_handle = GetModuleHandleW(module_name.data());
    if (!module_handle) {
        module_handle = LoadLibraryW(module_name.data());
        if (!module_handle) {
            PrintError(L"GetFunctionAddress: Failed to load module");
            return Scan();
        }
    }

    FARPROC function_address = GetProcAddress(module_handle, Utils::ToString(function_name).c_str());
    if (!function_address) {
        PrintError(L"GetFunctionAddress: Failed to get function address");
        return Scan();
    }

    MODULEINFO module_info;
    if (!GetModuleInformation(GetCurrentProcess(), module_handle, &module_info, sizeof(module_info))) {
        PrintError(L"GetFunctionAddress: Failed to get module information");
        return Scan();
    }

    return Scan(reinterpret_cast<std::uintptr_t>(function_address), { reinterpret_cast<size_t>(module_info.lpBaseOfDll), static_cast<size_t>(module_info.SizeOfImage) });
}

template <typename T>
bool ScanMatchNumeric(const T& val, const T& first, const T& second, ScanType scan_type) {
    if constexpr (std::is_same_v<T, std::wstring_view> || std::is_same_v<T, std::string_view>) {
        switch (scan_type) {
        case ScanType::Unknown:
            return true;
        case ScanType::Exact:
            return val == first;
        case ScanType::Contains:
            return std::search(val.begin(), val.end(), first.begin(), first.end()) != val.end();
        default:
            return false;
        }
    }
    else {
        switch (scan_type) {
        case ScanType::Unknown:
            return true;
        case ScanType::Exact:
            return val == first;
        case ScanType::GreaterThan:
            return val > first;
        case ScanType::LessThan:
            return val < first;
        case ScanType::Between:
            return val >= first && val <= second;
        default:
            return false;
        }
    }
}

bool PatternScanner::ScanMatch(const void* value, ScanTargets targets, ValueType value_type, ScanType scan_type)
{
    switch (value_type) {
    case ValueType::Byte: {
        std::int8_t val(*reinterpret_cast<const std::int8_t*>(value));
        std::int8_t first(*reinterpret_cast<const std::int8_t*>(targets.first.data()));
        std::int8_t second(targets.second.empty() ? 0 : *reinterpret_cast<const std::int8_t*>(targets.second.data()));
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    case ValueType::Int: {
        std::int32_t val(*reinterpret_cast<const std::int32_t*>(value));
        std::int32_t first(*reinterpret_cast<const std::int32_t*>(targets.first.data()));
        std::int32_t second(targets.second.empty() ? 0 : *reinterpret_cast<const std::int32_t*>(targets.second.data()));
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    case ValueType::Float: {
        float val(*reinterpret_cast<const float*>(value));
        float first(*reinterpret_cast<const float*>(targets.first.data()));
        float second(targets.second.empty() ? 0.0f : *reinterpret_cast<const float*>(targets.second.data()));
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    case ValueType::Double: {
        double val(*reinterpret_cast<const double*>(value));
        double first(*reinterpret_cast<const double*>(targets.first.data()));
        double second(targets.second.empty() ? 0.0 : *reinterpret_cast<const double*>(targets.second.data()));
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    case ValueType::String: {
        std::string_view val(reinterpret_cast<const char*>(value));
        std::string_view first(reinterpret_cast<const char*>(targets.first.data()), targets.first.size());
        std::string_view second(targets.second.empty() ? "" : reinterpret_cast<const char*>(targets.second.data()), targets.second.size());
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    case ValueType::WString: {
        std::wstring_view val(reinterpret_cast<const wchar_t*>(value));
        std::wstring_view first(reinterpret_cast<const wchar_t*>(targets.first.data()), targets.first.size());
        std::wstring_view second(targets.second.empty() ? L"" : reinterpret_cast<const wchar_t*>(targets.second.data()), targets.second.size());
        return ScanMatchNumeric(val, first, second, scan_type);
    }
    default:
        return false;
    }
}

std::vector<std::uint8_t> PatternScanner::SignatureToByteArray(std::wstring_view signature)
{
    std::vector<std::uint8_t> signature_bytes;
    std::wstring word;
    std::wistringstream iss(signature.data());

    while (iss >> word) {
        if ((word.size() == 1 && word[0] == L'?') || (word.size() == 2 && word[0] == L'?' && word[1] == L'?')) {
            signature_bytes.push_back(0);
        }
        else if (word.size() == 2 && std::isxdigit(word[0]) && std::isxdigit(word[1])) {
            unsigned long value = std::stoul(word, nullptr, 16);
            if (value <= 255) {
                std::uint8_t byte = static_cast<std::uint8_t>(value);
                signature_bytes.push_back(byte);
            }
            else {
                PrintError(L"SignatureToByteArray: Value out of range");
                return {};
            }
        }
        else {
            for (wchar_t c : word) {
                if (c >= 0 && c <= 255) {
                    signature_bytes.push_back(static_cast<std::uint8_t>(c));
                }
                else {
                    PrintError(L"SignatureToByteArray: Value out of range");
                    return {};
                }
            }
        }
    }

    return signature_bytes;
}

//std::vector<std::uint16_t> PatternScanner::SignatureToByteArray(std::wstring_view signature)
//{
//    std::vector<std::uint16_t> signature_bytes;
//    std::wstring word;
//    std::wistringstream iss(signature.data());
//
//    while (iss >> word) {
//        if ((word.size() == 1 && word[0] == L'?') || (word.size() == 2 && word[0] == L'?' && word[1] == L'?')) {
//            signature_bytes.push_back(0);
//        }
//        else if (word.size() == 2 && std::isxdigit(word[0]) && std::isxdigit(word[1])) {
//            unsigned long value = std::stoul(word, nullptr, 16);
//            if (value <= 65535) {
//                uint16_t byte = static_cast<uint16_t>(value);
//                signature_bytes.push_back(byte);
//            }
//            else {
//                PrintError(L"SignatureToByteArray: Value out of range");
//                return {};
//            }
//        }
//        else {
//            for (wchar_t c : word) {
//                if (c >= 0 && c <= 65535) {
//                    signature_bytes.push_back(static_cast<uint16_t>(c));
//                }
//                else {
//                    PrintError(L"SignatureToByteArray: Value out of range");
//                    return {};
//                }
//            }
//        }
//    }
//
//    return signature_bytes;
//}

std::vector<Scan> PatternScanner::ScanAll(std::size_t base_address, std::size_t image_size, ScanTargets byte_pattern, ValueType value_type, ScanType scan_type, bool forward)
{
    if (!base_address) {
        PrintError(L"ScanAll: Invalid base address ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return {};
    }

    if (!image_size) {
        PrintError(L"ScanAll: Invalid image size ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return {};
    }

    if (byte_pattern.first.empty() || byte_pattern.first.size() > image_size) {
        PrintError(L"ScanAll: Invalid pattern size ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return {};
    }

    const auto pattern_size = byte_pattern.first.size();
    const auto end_address = base_address + image_size - pattern_size;
    const auto first_byte = byte_pattern.first[0];
    std::vector<Scan> addresses;
    addresses.reserve(256);

    auto scan = [&](auto start, auto end, auto step) {
        for (std::size_t address = start; address != end; address += step) {
            if (reinterpret_cast<const std::uint8_t*>(address)[0] != first_byte) {
                continue;
            }

            bool found = true;
            for (size_t i = 1; i < pattern_size; ++i) {
                if (byte_pattern.first[i] != 0 && byte_pattern.first[i] != reinterpret_cast<const std::uint8_t*>(address)[i]) {
                    found = false;
                    break;
                }
            }

            if (found && ScanMatch(reinterpret_cast<const void*>(address), byte_pattern, value_type, scan_type)) {
                addresses.push_back(Scan(address, { base_address, image_size }));
            }
        }
    };

    if (forward) {
        scan(base_address, end_address + 1, 1);
    }
    else {
        scan(end_address + 1, base_address - 1, -1);
    }

    return addresses;
}

std::vector<Scan> PatternScanner::ScanAll(std::size_t base_address, std::size_t image_size, std::wstring_view value, ScanType scan_type, bool forward)
{
    return ScanAll(base_address, image_size, { SignatureToByteArray(value) }, ValueType::WString, scan_type, forward);
}

std::vector<Scan> PatternScanner::ScanAll(std::wstring_view value, std::wstring_view module_name, ScanType scan_type, bool forward)
{
    auto module = GetModuleInfo(module_name);
    return ScanAll(module.base_address, module.image_size, value, scan_type, forward);
}

Scan PatternScanner::ScanFirst(std::size_t base_address, std::size_t image_size, ScanTargets byte_pattern, ValueType value_type, ScanType scan_type, bool forward)
{
    if (!base_address) {
        PrintError(L"ScanFirst: Invalid base address ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return Scan();
    }

    if (!image_size) {
        PrintError(L"ScanFirst: Invalid image size ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return Scan();
    }

    if (byte_pattern.first.empty() || byte_pattern.first.size() > image_size) {
        PrintError(L"ScanFirst: Invalid pattern size ({})", value_type == ValueType::String || value_type == ValueType::WString ? std::string(byte_pattern.first.begin(), byte_pattern.first.end()) : Utils::ToHexString(byte_pattern.first.data(), byte_pattern.first.size()));
        return Scan();
    }
    const auto pattern_size = byte_pattern.first.size();
    const auto end_address = base_address + image_size - pattern_size;
    const auto first_byte = byte_pattern.first[0];

    auto scan = [&](auto start, auto end, auto step) {
        for (std::size_t address = start; address != end; address += step) {
            if (reinterpret_cast<const std::uint8_t*>(address)[0] != first_byte) {
                continue;
            }

            bool found = true;
            for (size_t i = 1; i < pattern_size; ++i) {
                if (byte_pattern.first[i] != 0 && byte_pattern.first[i] != reinterpret_cast<const std::uint8_t*>(address)[i]) {
                    found = false;
                    break;
                }
            }

            if (found && ScanMatch(reinterpret_cast<const void*>(address), byte_pattern, value_type, scan_type)) {
                return Scan(address, { base_address, image_size });
            }
        }
        return Scan(NULL, { base_address, image_size });
    };

    if (forward) {
        return scan(base_address, end_address + 1, 1);
    }
    else {
        return scan(end_address + 1, base_address - 1, -1);
    }
}

Scan PatternScanner::ScanFirst(std::size_t base_address, std::size_t image_size, std::wstring_view value, ScanType scan_type, bool forward)
{
    return ScanFirst(base_address, image_size, { SignatureToByteArray(value) }, ValueType::WString, scan_type, forward);
}

Scan PatternScanner::ScanFirst(std::wstring_view value, std::wstring_view module_name, ScanType scan_type, bool forward)
{
    auto module = GetModuleInfo(module_name);
    return ScanFirst(module.base_address, module.image_size, value, scan_type, forward);
}

Scan::Scan(std::uintptr_t address, std::pair<std::size_t, std::size_t> module_info) : m_address(address), m_module_info(module_info)/*, m_value(reinterpret_cast<const void*>(address))*/
{
    //...
}

Scan::operator std::uintptr_t() const
{
    return m_address;
}

void Scan::print_address(std::wstring_view name) const
{
    Print({ Color::Yellow , Color::Cyan }, L"{:X} {}", m_address, !name.empty() ? L" : " + std::wstring(name.data()) : L"");
}

bool Scan::is_found(const std::vector<std::uint8_t>& value) const
{
    if (m_address == NULL)
        return false;

    if (!value.empty()) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (*(reinterpret_cast<std::uint8_t*>(m_address) + i) != value[i])
                return false;
        }
    }

    return true;
}

std::uint8_t* Scan::data() const
{
    return reinterpret_cast<std::uint8_t*>(m_address);
}

Scan Scan::rva() const
{
    return is_found() ? Scan(m_address - m_module_info.first, m_module_info) : Scan(NULL, m_module_info);
}

Scan Scan::offset(std::size_t value) const
{
    if (is_found()) {
        if (value > 0) {
            return Scan(m_address + value, m_module_info);
        }
        else {
            return Scan(m_address - value, m_module_info);
        }
    }
    else {
        return Scan(NULL, m_module_info);
    }
}

Scan Scan::disassemble() const
{
    uint8_t opcode = *(uint8_t*)m_address;

    struct Instruction {
        std::wstring_view mnemonic;
        std::uintptr_t operand_value;
    } _Instruction{};

    switch (opcode) {
    case 0x68:
        _Instruction = { L"PUSH", *(uint32_t*)(m_address + 1) }; break;
    case 0x8B:
        _Instruction = { L"MOV", *(uint32_t*)(m_address + 2) }; break;
    case 0xB8:
        _Instruction = { L"MOV", *(uint32_t*)(m_address + 1) }; break;
    case 0xC7:
        _Instruction = { L"MOV", *(uint32_t*)(m_address + 2) }; break;
    case 0xE8:
        _Instruction = { L"CALL", *(uint32_t*)(m_address + 1) + m_address + 5 }; break;
    case 0x74:
        _Instruction = { L"JE", m_address + 2 + *(int8_t*)(m_address + 1) }; break;
    case 0x75:
        _Instruction = { L"JNE", m_address + 2 + *(int8_t*)(m_address + 1) }; break;
    case 0x3B:
        _Instruction = { L"CMP", *(uint32_t*)(m_address + 2) }; break;
    case 0xEB:
        _Instruction = { L"JMP", m_address + 2 + *(int8_t*)(m_address + 1) }; break;
    case 0xE9:
        _Instruction = { L"JMP", *(uint32_t*)(m_address + 1) + m_address + 5 }; break;
    case 0x7F:
        _Instruction = { L"JG", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x7D:
        _Instruction = { L"JGE", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x7C:
        _Instruction = { L"JL", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x7E:
        _Instruction = { L"JLE", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x7A:
        _Instruction = { L"JP", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x7B:
        _Instruction = { L"JNP", m_address + 6 + *(int32_t*)(m_address + 2) }; break;
    case 0x83:
        _Instruction = { L"ADD", (uint8_t)(m_address + 1) }; break;
    case 0x81:
        _Instruction = { L"ADD", (uint32_t)(m_address + 2) }; break;
    default:
        PrintError(L"Disassemble: Unknown opcode encountered: {:X}", opcode);
        return Scan();
    }

    //Print(L"{:X} | {} {:X}", m_address, _Instruction.mnemonic, _Instruction.operand_value);
    return Scan(_Instruction.operand_value, m_module_info);
}

void** Scan::hook(void* hook_function) const
{
    return (is_found() && Hooking::HookFunction(&(void*&)m_address, hook_function)) ? reinterpret_cast<void**>(m_address) : NULL;
}

bool Scan::unhook() const
{
    return is_found() ? Hooking::UnhookFunction(&(PVOID&)m_address) : false;
}

Scan Scan::scan_first(std::wstring_view value, ScanType scan_type, bool forward) const
{
    return is_found() ? PatternScanner::ScanFirst(m_address, m_module_info.second - rva(), { PatternScanner::SignatureToByteArray(value) }, ValueType::WString, scan_type, forward) : Scan(NULL, m_module_info);
}

std::vector<Scan> Scan::get_all_matching_codes(std::vector<std::uint8_t> pattern, bool check_displacement, std::size_t base_address, std::size_t image_size) const
{
    std::vector<Scan> addresses;
    auto pattern_scan = PatternScanner::ScanAll(base_address == 0 ? m_module_info.first : base_address, image_size == 0 ? m_module_info.second : image_size, Utils::ToHexWideString(pattern), ScanType::Unknown, true);
    for (const auto& it : pattern_scan) {
        const auto instruction_address = static_cast<std::int32_t>(it);
        const auto target_address = static_cast<std::int32_t>(m_address);
        const auto displacement = target_address - instruction_address - pattern.size() - sizeof(std::int32_t);
        const auto expected_value = check_displacement ? displacement : target_address;
        if (*reinterpret_cast<const std::int32_t*>(it + pattern.size()) == expected_value)
            addresses.push_back(Scan(it, { base_address == 0 ? m_module_info.first : base_address, image_size == 0 ? m_module_info.second : image_size }));
    }
    return addresses;
}

Scan Scan::get_first_matching_code(std::vector<std::uint8_t> pattern, bool check_displacement, std::size_t base_address, std::size_t image_size) const
{
    auto pattern_scan = PatternScanner::ScanAll(base_address == 0 ? m_module_info.first : base_address, image_size == 0 ? m_module_info.second : image_size, Utils::ToHexWideString(pattern), ScanType::Unknown, true);
    for (const auto& it : pattern_scan) {
        const auto instruction_address = static_cast<std::int32_t>(it);
        const auto target_address = static_cast<std::int32_t>(m_address);
        const auto displacement = target_address - instruction_address - pattern.size() - sizeof(std::int32_t);
        const auto expected_value = check_displacement ? displacement : target_address;
        if (*reinterpret_cast<const std::int32_t*>(it + pattern.size()) == expected_value)
            return Scan(it, { base_address == 0 ? m_module_info.first : base_address, image_size == 0 ? m_module_info.second : image_size });
    }
    return Scan();
}