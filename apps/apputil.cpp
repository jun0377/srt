/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <cstring>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <memory>

#include "srt.h" // Required for SRT_SYNC_CLOCK_* definitions.
#include "apputil.hpp"
#include "netinet_any.h"
#include "srt_compat.h"

using namespace std;
using namespace srt;


// NOTE: MINGW currently does not include support for inet_pton(). See
//    http://mingw.5.n7.nabble.com/Win32API-request-for-new-functions-td22029.html
//    Even if it did support inet_pton(), it is only available on Windows Vista
//    and later. Since we need to support WindowsXP and later in ORTHRUS. Many
//    customers still use it, we will need to implement using something like
//    WSAStringToAddress() which is available on Windows95 and later.
//    Support for IPv6 was added on WindowsXP SP1.
// Header: winsock2.h
// Implementation: ws2_32.dll
// See:
//    https://msdn.microsoft.com/en-us/library/windows/desktop/ms742214(v=vs.85).aspx
//    http://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedInternet3b.html
#if defined(_WIN32) && !defined(HAVE_INET_PTON)
namespace // Prevent conflict in case when still defined
{
int inet_pton(int af, const char * src, void * dst)
{
   struct sockaddr_storage ss;
   int ssSize = sizeof(ss);
   char srcCopy[INET6_ADDRSTRLEN + 1];

   ZeroMemory(&ss, sizeof(ss));

   // work around non-const API
#ifdef _MSC_VER
   strncpy_s(srcCopy, INET6_ADDRSTRLEN + 1, src, _TRUNCATE);
#else
   strncpy(srcCopy, src, INET6_ADDRSTRLEN);
   srcCopy[INET6_ADDRSTRLEN] = '\0';
#endif
   if (WSAStringToAddress(
      srcCopy, af, NULL, (struct sockaddr *)&ss, &ssSize) != 0)
   {
      return 0;
   }

   switch (af)
   {
      case AF_INET :
      {
         *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
         return 1;
      }
      case AF_INET6 :
      {
         *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
         return 1;
      }
      default :
      {
         // No-Op
      }
   }

   return 0;
}
}
#endif // _WIN32 && !HAVE_INET_PTON

// Create sockaddr_any address
sockaddr_any CreateAddr(const string& name, unsigned short port, int pref_family)
{
    // Handle empty name.
    // If family is specified, empty string resolves to ANY of that family.
    // If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
    
    // ip any
    if (name == "")
    {
        sockaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
        result.hport(port);
        return result;
    }

    // IPv6 Perferred,优先尝试IPv6
    bool first6 = pref_family != AF_INET;
    int families[2] = {AF_INET6, AF_INET};
    if (!first6)
    {
        families[0] = AF_INET;
        families[1] = AF_INET6;
    }

    // 按优先级尝试解析IP地址
    for (int i = 0; i < 2; ++i)
    {
        int family = families[i];
        sockaddr_any result (family);

        // Try to resolve the name by pton first
        if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
        {
            result.hport(port); // same addr location in ipv4 and ipv6
            return result;
        }
    }

    // 由于inet_pton无法处理域名格式的地址，如：www.baidu.com，因此尝试使用getaddrinfo进行域名和服务器类型的转换

    // If not, try to resolve by getaddrinfo
    // This time, use the exact value of pref_family

    sockaddr_any result;
    addrinfo fo = {
        0,
        pref_family,
        0, 0,
        0, 0,
        NULL, NULL
    };

    addrinfo* val = nullptr;
    int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
    if (erc == 0)
    {
        result.set(val->ai_addr);
        result.len = result.size();
        result.hport(port); // same addr location in ipv4 and ipv6
    }
    freeaddrinfo(val);

    return result;
}

// 使用指定的分隔符sep拼接字符串
string Join(const vector<string>& in, string sep)
{
    if ( in.empty() )
        return "";

    ostringstream os;

    os << in[0];
    for (auto i = in.begin()+1; i != in.end(); ++i)
        os << sep << *i;
    return os.str();
}

// OPTION LIBRARY

OptionScheme::Args OptionName::DetermineTypeFromHelpText(const std::string& helptext)
{
    if (helptext.empty())
        return OptionScheme::ARG_NONE;

    if (helptext[0] == '<')
    {
        // If the argument is <one-argument>, then it's ARG_NONE.
        // If it's <multiple-arguments...>, then it's ARG_VAR.
        // When closing angle bracket isn't found, fallback to ARG_ONE.
        size_t pos = helptext.find('>');
        if (pos == std::string::npos)
            return OptionScheme::ARG_ONE; // mistake, but acceptable

        if (pos >= 4 && helptext.substr(pos-3, 4) == "...>")
            return OptionScheme::ARG_VAR;

        // We have < and > without ..., simply one argument
        return OptionScheme::ARG_ONE;
    }

    if (helptext[0] == '[')
    {
        // Argument in [] means it is optional; in this case
        // you should state that the argument can be given or not.
        return OptionScheme::ARG_VAR;
    }

    // Also as fallback
    return OptionScheme::ARG_NONE;
}

// 解析命令行参数，并将所有参数通过一个map返回
options_t ProcessOptions(char* const* argv, int argc, std::vector<OptionScheme> scheme)
{
    using namespace std;

    string current_key; // 命令行参数-key
    string extra_arg;   // 命令行参数-value
    size_t vals = 0;    // 参数个数计数
    OptionScheme::Args type = OptionScheme::ARG_VAR; // This is for no-option-yet or consumed
    map<string, vector<string>> params;
    bool moreoptions = true;    // 是否是一个需要参数的命令行选项

    // argv + 1 : 从第二个字符串开始遍历，第一个参数是程序名
    for (char* const* p = argv+1; p != argv+argc; ++p)
    {
        const char* a = *p;
        // cout << "*D ARG: '" << a << "'\n";

        // 判断是否是一个合法的命令行参数选项,命令行参数必须以 '-' 开头
        bool isoption = false;
        if (a[0] == '-')
        {
            isoption = true;
            // If a[0] isn't NUL - because it is dash - then
            // we can safely check a[1].
            // An expression starting with a dash is not
            // an option marker if it is a single dash or
            // a negative number.

            // 命令行参数为空，或者是一个'-'+数字，则认为是一个无效的命令行选项
            // 如："-123" 这表示一个负数，而不是一个命令行选项
            if (!a[1] || isdigit(a[1]))
                isoption = false;
        }

        // 需要参数的命令行选项
        if (moreoptions && isoption)
        {
            bool arg_specified = false;     // 当前参数是否已经被处理过
            size_t seppos;                  // 分隔符索引位置，(see goto, it would jump over initialization)

            // 命令行选项以 "--" 开头，则认为是一个不需要额外参数的选项，如 --verbose
            current_key = a+1;
            if ( current_key == "-" )
            {
                // The -- argument terminates the options.
                // The default key is restored to empty so that
                // it collects now all arguments under the empty key
                // (not-option-assigned argument).
                moreoptions = false;
                goto EndOfArgs;
            }

            // Maintain the backward compatibility with argument specified after :
            // or with one string separated by space inside.

            // 命令行参数的两种兼容写法：以:分割，-option:value 或 以空格分隔，-option value
            
            // 查找分割符，首先检查分隔符':',兼容空格分隔符
            seppos = current_key.find(':');
            if (seppos == string::npos)
                seppos = current_key.find(' ');

            // 分隔符前后分别为key和value,将当前参数标记为已处理
            if (seppos != string::npos)
            {
                // Old option specification.
                // 命令行参数-value
                extra_arg = current_key.substr(seppos + 1);
                // 命令行参数-key
                current_key = current_key.substr(0, 0 + seppos);
                // 当前参数已被处理过
                arg_specified = true; // Prevent eating args from option list
            }

            // 将当前命令行参数key和value添加到map中
            params[current_key].clear();
            vals = 0;
            if (extra_arg != "")
            {
                params[current_key].push_back(extra_arg);
                ++vals;
                extra_arg.clear();
            }

            // Find the key in the scheme. If not found, treat it as ARG_NONE.

            // 遍历所有自定义的命令行选项，找到当前正在处理的命令行参数
            for (const auto& s: scheme)
            {
                // 找到了当前选项
                if (s.names().count(current_key))
                {
                    // cout << "*D found '" << current_key << "' in scheme type=" << int(s.type) << endl;
                    // If argument was specified using the old way, like
                    // -v:0 or "-v 0", then consider the argument specified and
                    // treat further arguments as either no-option arguments or
                    // new options.

                    // 当前选项不需要参数，或参数成功处理
                    if (s.type == OptionScheme::ARG_NONE || arg_specified)
                    {
                        // Anyway, consider it already processed.
                        break;
                    }
                    type = s.type;

                    // 只需要一个参数，并且参数已解析
                    if ( vals == 1 && type == OptionScheme::ARG_ONE )
                    {
                        // Argument for one-arg option already consumed,
                        // so set to free args.
                        goto EndOfArgs;
                    }
                    goto Found;
                }

            }
            // Not found: set ARG_NONE.
            // cout << "*D KEY '" << current_key << "' assumed type NONE\n";
EndOfArgs:
            type = OptionScheme::ARG_VAR;
            current_key = "";
Found:
            continue;
        }

        // Collected a value - check if full
        // cout << "*D COLLECTING '" << a << "' for key '" << current_key << "' (" << vals << " so far)\n";
        params[current_key].push_back(a);
        ++vals;
        if ( vals == 1 && type == OptionScheme::ARG_ONE )
        {
            // cout << "*D KEY TYPE ONE - resetting to empty key\n";
            // Reset the key to "default one".
            current_key = "";
            vals = 0;
            type = OptionScheme::ARG_VAR;
        }
        else
        {
            // cout << "*D KEY type VAR - still collecting until the end of options or next option.\n";
        }
    }

    return params;
}

string OptionHelpItem(const OptionName& o)
{
    string out = "\t-" + o.main_name;
    string hlp = o.helptext;
    string prefix;

    if (hlp == "")
    {
        hlp = " (Undocumented)";
    }
    else if (hlp[0] != ' ')
    {
        size_t end = string::npos;
        if (hlp[0] == '<')
        {
            end = hlp.find('>');
        }
        else if (hlp[0] == '[')
        {
            end = hlp.find(']');
        }

        if (end != string::npos)
        {
            ++end;
        }
        else
        {
            end = hlp.find(' ');
        }

        if (end != string::npos)
        {
            prefix = hlp.substr(0, end);
            //while (hlp[end] == ' ')
            //    ++end;
            hlp = hlp.substr(end);
            out += " " + prefix;
        }
    }

    out += " -" + hlp;
    return out;
}

const char* SRTClockTypeStr()
{
    const int clock_type = srt_clock_type();

    switch (clock_type)
    {
    case SRT_SYNC_CLOCK_STDCXX_STEADY:
        return "CXX11_STEADY";
    case SRT_SYNC_CLOCK_GETTIME_MONOTONIC:
        return "GETTIME_MONOTONIC";
    case SRT_SYNC_CLOCK_WINQPC:
        return "WIN_QPC";
    case SRT_SYNC_CLOCK_MACH_ABSTIME:
        return "MACH_ABSTIME";
    case SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY:
        return "POSIX_GETTIMEOFDAY";
    default:
        break;
    }
    
    return "UNKNOWN VALUE";
}

void PrintLibVersion()
{
    cerr << "Built with SRT Library version: " << SRT_VERSION  << endl;
    const uint32_t srtver = srt_getversion();
    const int major = srtver / 0x10000;
    const int minor = (srtver / 0x100) % 0x100;
    const int patch = srtver % 0x100;
    cerr << "SRT Library version: " << major << "." << minor << "." << patch << ", clock type: " << SRTClockTypeStr() << endl;
}
