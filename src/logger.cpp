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

#ifndef NDEBUG

#include <iostream>
#include <sstream>

#include "logger.hpp"
#include "util.hpp"

#include <windows.h>

namespace {
    // trim from start
    inline std::string ltrim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    // trim from end
    inline std::string rtrim(std::string s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    // trim from both ends
    inline std::string trim(std::string s) {
        return ltrim(rtrim(s));
    }

    class MyLogger : public std::stringbuf {
    public:
        virtual int sync()
        {
            std::string u8 = trim(this->str());
            std::wstring u16 = std::wstring(L"DD4Seven DEBUG LOG: ") + util::utf8_to_utf16(u8) + std::wstring(L"\r\n");

            OutputDebugString(u16.c_str());

            this->str(std::string());

            return 0;
        }
    };
}

std::ostream* get_logger()
{
    static __thread MyLogger     *my_buf = nullptr;
    static __thread std::ostream *my_stream = nullptr;

    if (!my_buf)
        my_buf = new MyLogger;
    if (!my_stream)
        my_stream = new std::ostream(my_buf);

    return my_stream;
}

#endif // NDEBUG
