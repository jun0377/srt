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
#include "sync.h"

namespace Verbose
{

extern bool on;                                    // 是否启用详细日志输出
extern std::ostream* cverb;

struct LogNoEol { LogNoEol() {} };
struct LogLock { LogLock() {} };

// verbose日志输出类
class Log
{
    bool noeol = false;                         // 是否自动添加换行符
    srt::sync::atomic<bool> lockline;

    // Disallow creating dynamic objects
    void* operator new(size_t) = delete;        // 禁止动态分配该对象

public:

    Log() {}
    Log(const Log& ) {}


    template <class V>
    Log& operator<<(const V& arg)
    {
        // Template - must be here; extern template requires
        // predefined specializations.
        if (on)
            (*cverb) << arg;
        return *this;
    }

    // 特化流操作运算符
    Log& operator<<(LogNoEol);      // 处理不输出换行符的情况，如：Verb() << "This is a message without newline" << VerbNoEOL;
    Log& operator<<(LogLock);       // 处理行锁定的情况，如：Verb() << "This is a thread-safe message" << VerbLock;
    ~Log();
};

// verbose error日志输出类
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
    out << arg1;
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

template <typename... Args>
inline void Verror(Args&&... args)
{
    Verbose::ErrLog log;
    Verbose::Print(log, args...);
}


// Manipulator tags
static const Verbose::LogNoEol VerbNoEOL;
static const Verbose::LogLock VerbLock;

#endif
