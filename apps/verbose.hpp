/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_VERBOSE_HPP
#define INC_SRT_VERBOSE_HPP

#include <iostream>
#if SRT_ENABLE_VERBOSE_LOCK
#include <mutex>
#endif

namespace Verbose
{

// 使用extern声明外部变量，只是声明，定义在verbose.cpp中
extern bool on;
extern std::ostream* cverb;

struct LogNoEol { LogNoEol() {} };  // 空结构体，只有一个默认构造函数
#if SRT_ENABLE_VERBOSE_LOCK
struct LogLock { LogLock() {} };
#endif

// 日志信息
class Log
{
    // 用于控制日志输出是否在末尾添加换行符
    bool noeol = false;

    // 用于控制日志记录是否加锁
#if SRT_ENABLE_VERBOSE_LOCK
    bool lockline = false;
#endif

    // Disallow creating dynamic objects
    // 重载new运算符，并作为私有函数，使得无法通过new运算符创建Log对象
    void* operator new(size_t);

public:

    // 重载 << 运算符模板，允许输出任意类型的数据到日志流中
    template <class V>
    Log& operator<<(const V& arg)
    {
        // Template - must be here; extern template requires
        // predefined specializations.
        if (on)
            (*cverb) << arg;
        return *this;
    }

    // LogNoEol 用作占位参数，指示日志输出是否在末尾添加换行符
    Log& operator<<(LogNoEol);
#if SRT_ENABLE_VERBOSE_LOCK
    Log& operator<<(LogLock);
#endif
    ~Log();
};


class ErrLog: public Log
{
public:

    template <class V>
    ErrLog& operator<<(const V& arg)
    {
        // Template - must be here; extern template requires
        // predefined specializations.
        if (on)
            (*cverb) << arg;
        else
            std::cerr << arg;
        return *this;
    }
};

// terminal
inline void Print(Log& ) {}

template <typename Arg1, typename... Args>
inline void Print(Log& out, Arg1&& arg1, Args&&... args)
{
    out << std::forward(arg1);
    Print(out, args...);
}

}

inline Verbose::Log Verb() { return Verbose::Log(); }
inline Verbose::ErrLog Verror() { return Verbose::ErrLog(); }

template <typename... Args>
inline void Verb(Args&&... args)
{
    Verbose::Log log;
    Verbose::Print(log, args...);
}


// Manipulator tags
static const Verbose::LogNoEol VerbNoEOL;
#if SRT_ENABLE_VERBOSE_LOCK
static const Verbose::LogLock VerbLock;
#endif

#endif
