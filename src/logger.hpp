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

#include <ostream>

#ifdef NDEBUG

// In release builds, we want to get rid of the logger altogether
template<typename charT, typename traitsT = std::char_traits<charT>>
struct null_logger_t {};

template<typename charT, typename traitsT, typename T>
null_logger_t<charT, traitsT> operator<<(null_logger_t<charT, traitsT> null, T) { return null; }

template<typename charT, typename traitsT>
null_logger_t<charT, traitsT> operator<<(null_logger_t<charT, traitsT> null, decltype(std::endl<charT, traitsT>)) { return null; }

#define logger (null_logger_t<char>())

#else

// The logger is thread_local!
std::ostream *get_logger();

#define logger (*get_logger())

#endif //NDEBUG
