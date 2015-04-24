// Copyright (C) 2015 Jonas Kümmerlin <rgcjonas@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <windows.h>

#include <memory>
#include <algorithm>
#include <cstring>
#include <cassert>

namespace util {
    ///////////////////////////////////////////////////////////////
    // Free-standing, independent utility functions
    ///////////////////////////////////////////////////////////////
    template <typename T>
    inline T clamp(const T& lower, const T& n, const T& upper) {
        return std::max(lower, std::min(n, upper));
    }

    inline std::wstring utf8_to_utf16(const std::string& str)
    {
        std::wstring wide;

        int needed_buffer = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (!needed_buffer)
            return std::wstring();

        wide.resize(needed_buffer, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wide[0], needed_buffer);

        return wide;
    }

    inline std::string wcsdup_to_codepage(UINT codepage, const wchar_t *utf16)
    {
        std::string ansi;

        int needed_buffer = WideCharToMultiByte(codepage, 0, utf16, -1, nullptr, 0, nullptr, nullptr);
        if (!needed_buffer)
            return std::string();

        ansi.resize(needed_buffer, 0);
        WideCharToMultiByte(codepage, 0, utf16, -1, &ansi[0], needed_buffer, nullptr, nullptr);

        return ansi;
    }

    inline std::string wcsdup_to_utf8(const wchar_t *utf16)
    {
        return wcsdup_to_codepage(CP_UTF8, utf16);
    }

    inline std::string hresult_to_utf8(HRESULT hr)
    {
        wchar_t    *buffer;
        std::string utf8;

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            hr,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        utf8 = wcsdup_to_utf8(buffer);

        LocalFree(buffer);

        return utf8;
    }

    inline uint64_t milliseconds_now() {
        static LARGE_INTEGER frequency;
        static BOOL qpcAvailable = QueryPerformanceFrequency(&frequency);
        if (qpcAvailable) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            return (1000LL * now.QuadPart) / frequency.QuadPart;
        } else {
            return GetTickCount();
        }
    }

    /**
     * return the next multiple of @param n being >= @param arg
     */
    template<typename T>
    inline T next_multiple(T n, T arg)
    {
        if (arg % n == 0) return arg;
        else return (arg / n) * n + n;
    }

    /**
     * return the pixel value in an indexed pixel format with a bpp <= 8
     */
    template<unsigned bpp>
    inline uint8_t get_pixel_from_row(uint8_t *row, int x) {
        return (row[x * bpp / 8] >> (8 - bpp - (x % (8 / bpp)) * bpp)) & ((1 << bpp) - 1);
    }

    template<typename TComparator = std::greater_equal<DWORD>>
    inline bool check_windows_version(DWORD major, DWORD minor, const TComparator& compare = TComparator())
    {
        OSVERSIONINFO v;
        v.dwOSVersionInfoSize = sizeof(v);

        if (!GetVersionEx(&v))
            return false;

        if (v.dwPlatformId != VER_PLATFORM_WIN32_NT)
            return false;

        //HACK: synthesize a compound version number
        //      This will be wrong once we reach NT 65536.X or X.65536
        DWORD haveVersion = (clamp<DWORD>(0, v.dwMajorVersion, 0xFFFF) << 16) | clamp<DWORD>(0, v.dwMinorVersion, 0xFFFF);
        DWORD wantVersion = (clamp<DWORD>(0, major, 0xFFFF) << 16) | clamp<DWORD>(0, minor, 0xFFFF);

        return compare(haveVersion, wantVersion);
    }


    enum class calling_convention { stdcall, c, fastcall };

    template<calling_convention convention, typename TReturn, typename... TArgs>
    struct attach_calling_convention;

    template<typename TReturn, typename... TArgs>
    struct attach_calling_convention<calling_convention::stdcall, TReturn(TArgs...)>
    {
        typedef TReturn(__stdcall *type)(TArgs...);
    };

    template<typename TReturn, typename... TArgs>
    struct attach_calling_convention<calling_convention::c, TReturn(TArgs...)>
    {
        typedef TReturn(__cdecl *type)(TArgs...);
    };

    template<typename TReturn, typename... TArgs>
    struct attach_calling_convention<calling_convention::fastcall, TReturn(TArgs...)>
    {
        typedef TReturn(__fastcall *type)(TArgs...);
    };

    template<typename TFunc, calling_convention conv = calling_convention::stdcall> class dll_func;

    template<typename TReturn, calling_convention conv, typename... TArgs>
    class dll_func<TReturn(TArgs...), conv>
    {
        HMODULE m_module = 0;
        typename attach_calling_convention<conv, TReturn(TArgs...)>::type m_function = nullptr;

    public:
        dll_func(const wchar_t *dll, const char *function)
        {
            m_module = LoadLibraryW(dll);
            if (!m_module) return;

            m_function = reinterpret_cast<decltype(m_function)>(GetProcAddress(m_module, function));
        }
        dll_func(const char *dll, const char *function)
        {
            m_module = LoadLibraryA(dll);
            if (!m_module) return;

            m_function = reinterpret_cast<decltype(m_function)>(GetProcAddress(m_module, function));
        }
        dll_func(const dll_func& other) = delete;
        dll_func(dll_func&& other) = delete;

        dll_func& operator=(const dll_func& other) = delete;
        dll_func& operator=(dll_func&& other) = delete;

        operator bool()
        {
            return m_module && m_function;
        }

        TReturn operator()(TArgs&&... args)
        {
            assert(m_function);

            return m_function(std::forward<TArgs>(args)...);
        }

        auto raw_func_ptr() -> decltype(m_function)
        {
            return m_function;
        }

        ~dll_func()
        {
            FreeLibrary(m_module);
        }
    };
}

