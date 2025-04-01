/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#ifndef INC_SRT_APPCOMMON_H
#define INC_SRT_APPCOMMON_H

#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>

#include "netinet_any.h"
#include "utilities.h"
#include "srt.h"

#if _WIN32

// Keep this below commented out.
// This is for a case when you need cpp debugging on Windows.
//#ifdef _WINSOCKAPI_
//#error "You include <winsock.h> somewhere, remove it. It causes conflicts"
//#endif

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// WIN32 API does not have sleep() and usleep(), Although MINGW does.
#ifdef __MINGW32__
#include <unistd.h>
#else
extern "C" inline int sleep(int seconds) { Sleep(seconds * 1000); return 0; }
#endif

inline bool SysInitializeNetwork()
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    return WSAStartup(wVersionRequested, &wsaData) == 0;
}

inline void SysCleanupNetwork()
{
    WSACleanup();
}

#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Fixes Android build on NDK r16b and earlier.
#if defined(__ANDROID__) && (__ANDROID__ == 1)
   #include <android/ndk-version.h>
   #if !defined(__NDK_MAJOR__) || (__NDK_MAJOR__ <= 16)
      struct ip_mreq_sourceFIXED {
        struct in_addr imr_multiaddr;
        struct in_addr imr_interface;
        struct in_addr imr_sourceaddr;
      };
      #define ip_mreq_source ip_mreq_sourceFIXED
   #endif
#endif

// Nothing needs to be done on POSIX; this is a Windows problem.
inline bool SysInitializeNetwork() {return true;}
inline void SysCleanupNetwork() {}

#endif

#ifdef _WIN32
inline int SysError() { return ::GetLastError(); }
const int SysAGAIN = WSAEWOULDBLOCK;
#else
inline int SysError() { return errno; }
const int SysAGAIN = EAGAIN;
#endif

srt::sockaddr_any CreateAddr(const std::string& name, unsigned short port = 0, int pref_family = AF_UNSPEC);
std::string Join(const std::vector<std::string>& in, std::string sep);

template <class VarType, class ValType>
struct OnReturnSetter
{
    VarType& var;
    ValType value;

    OnReturnSetter(VarType& target, ValType v): var(target), value(v) {}
    ~OnReturnSetter() { var = value; }
};

template <class VarType, class ValType>
OnReturnSetter<VarType, ValType> OnReturnSet(VarType& target, ValType v)
{ return OnReturnSetter<VarType, ValType>(target, v); }

// ---- OPTIONS MODULE

inline bool CheckTrue(const std::vector<std::string>& in)
{
    if (in.empty())
        return true;

    const std::set<std::string> false_vals = { "0", "no", "off", "false" };
    if (false_vals.count(in[0]))
        return false;

    return true;

    //if (in[0] != "false" && in[0] != "off")
    //    return true;

    //return false;
}

// 通用的字符串转数值模板函数，必须特化才能使用
template<class Number>
static inline Number StrToNumber(const std::string& )
{
    typename Number::incorrect_version wrong = Number::incorrect_version;   // 通过访问不存在的incorrect_version来防止用户使用特化的数值类型
    return Number();
}

#define STON(type, function) \
template<> inline type StrToNumber(const std::string& s) { return function (s, 0, 0); }

STON(int, stoi);
STON(unsigned long, stoul);
STON(unsigned int, stoul);
STON(long long, stoll);
STON(unsigned long long, stoull);

#undef STON

typedef std::map<std::string, std::vector<std::string>> options_t;

struct OutList
{
    typedef std::vector<std::string> type;
    static type process(const options_t::mapped_type& i) { return i; }
};

// 使用空格拼接字符串
struct OutString
{
    typedef std::string type;
    static type process(const options_t::mapped_type& i) { return Join(i, " "); }
};

// 字符串转数值
struct NumberAutoConvert
{
    std::string value;  // 要转化的字符串

    NumberAutoConvert(): NumberAutoConvert("") {}
    NumberAutoConvert(const std::string& arg): NumberAutoConvert(arg.c_str()) {}
    NumberAutoConvert(const char* arg): value(arg)
    {
        if (value.empty())
            value = "0"; // Must convert to a default 0 number
    }

    // 函数模板，特化函数模板StrToNumber
    template<class Number>
    operator Number()
    {
        return StrToNumber<Number>(value);
    }
};

// 命令行参数中的数值类型转换
struct OutNumber
{
    typedef NumberAutoConvert type;
    static type process(const options_t::mapped_type& i)
    {
        // Numbers can't be joined, use the "last overrides" rule.
        if (i.empty())
            return {"0"};

        return type { i.back() };
    }
};

template <class Number>
struct OutNumberAs
{
    typedef Number type;
    static type process(const options_t::mapped_type& i)
    {
        return OutNumber::process(i);
    }
};


struct OutBool
{
    typedef bool type;
    static type process(const options_t::mapped_type& i) { return CheckTrue(i); }
};

struct OptionName;

// 命令行参数的选项方案
struct OptionScheme
{
    // 选项名称
    const OptionName* pid;
    // 选项参数方案：无参数/一个参数/可变参数
    enum Args { ARG_NONE, ARG_ONE, ARG_VAR } type;

    // 默认拷贝构造函数
    OptionScheme(const OptionScheme&) = default;
    // 移动构造函数，虽然实现了移动语义，但是并没有清空源对象的状态，因为此类所有的成员都可以被安全共享
    OptionScheme(OptionScheme&& src)
        : pid(src.pid)
        , type(src.type)
    {
    }

    // 基本构造函数
    OptionScheme(const OptionName& id, Args tp);

    // 获取所有参数别名
    const std::set<std::string>& names() const;
};

// 用户可配置的参数
struct OptionName
{
    std::string helptext;           // 帮助信息
    std::string main_name;          // 选项名称
    std::set<std::string> names;    // 所有的选项别名，可定义多个选项别名，便于用户理解

    // 基本构造函数
    template <class... Args>
    OptionName(std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
    }

    // 带选项方案的构造函数
    template <class... Args>
    OptionName(std::vector<OptionScheme>& sc, OptionScheme::Args type,
            std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
        sc.push_back(OptionScheme(*this, type));
    }

    // 带参数自动类型推导的构造函数
    template <class... Args>
    OptionName(std::vector<OptionScheme>& sc,
            std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
        OptionScheme::Args type = DetermineTypeFromHelpText(ht);
        sc.push_back(OptionScheme(*this, type));
    }

    // 初始化列表构造函数，可一次初始化多个选项别名
    OptionName(std::initializer_list<std::string> args): main_name(*args.begin()), names(args) {}

    // 类型转换运算符重载
    operator std::set<std::string>() { return names; }
    operator const std::set<std::string>() const { return names; }

private:
    static OptionScheme::Args DetermineTypeFromHelpText(const std::string& helptext);
};

inline OptionScheme::OptionScheme(const OptionName& id, Args tp): pid(&id), type(tp) {}
inline const std::set<std::string>& OptionScheme::names() const { return pid->names; }

template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t&, OutValue deflt=OutValue()) { return deflt; }

template <class OutType, class OutValue, class... Args> inline
typename OutType::type Option(const options_t& options, OutValue deflt, std::string key, Args... further_keys)
{
    auto i = options.find(key);
    if ( i == options.end() )
        return Option<OutType>(options, deflt, further_keys...);
    return OutType::process(i->second);
}

template<typename TrapType>
struct OptionTrapType
{
    static TrapType pass(TrapType v) { return v; }
};

template<>
struct OptionTrapType<const char*>
{
    static std::string pass(const char* v) { return v; }
};

template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t& options, OutValue deflt, const OptionName& oname)
{
    (void)OptionTrapType<OutValue>::pass(deflt);
    for (auto key: oname.names)
    {
        auto i = options.find(key);
        if ( i != options.end() )
        {
            return OutType::process(i->second);
        }
    }
    return deflt;
}

template <class OutType> inline
typename OutType::type Option(const options_t& options, const OptionName& oname)
{
    typedef typename OutType::type out_t;
    for (auto key: oname.names)
    {
        auto i = options.find(key);
        if ( i != options.end() )
        {
            return OutType::process(i->second);
        }
    }
    return out_t();
}

// 从map中查找指定选项
inline bool OptionPresent(const options_t& options, const std::set<std::string>& keys)
{
    for (auto key: keys)
    {
        auto i = options.find(key);
        if ( i != options.end() )
            return true;
    }
    return false;
}

options_t ProcessOptions(char* const* argv, int argc, std::vector<OptionScheme> scheme);
std::string OptionHelpItem(const OptionName& o);

const char* SRTClockTypeStr();
void PrintLibVersion();


namespace srt
{

struct OptionSetterProxy
{
    SRTSOCKET s;
    int result = -1;

    OptionSetterProxy(SRTSOCKET ss): s(ss) {}

    struct OptionProxy
    {
        OptionSetterProxy& parent;
        SRT_SOCKOPT opt;

#define SPEC(type) \
        OptionProxy& operator=(const type& val)\
        {\
            parent.result = srt_setsockflag(parent.s, opt, &val, sizeof val);\
            return *this;\
        }

        SPEC(int32_t);
        SPEC(int64_t);
        SPEC(bool);
#undef SPEC

        template<size_t N>
        OptionProxy& operator=(const char (&val)[N])
        {
            parent.result = srt_setsockflag(parent.s, opt, val, N-1);
            return *this;
        }

        OptionProxy& operator=(const std::string& val)
        {
            parent.result = srt_setsockflag(parent.s, opt, val.c_str(), val.size());
            return *this;
        }
    };

    OptionProxy operator[](SRT_SOCKOPT opt)
    {
        return OptionProxy {*this, opt};
    }

    operator int() { return result; }
};

inline OptionSetterProxy setopt(SRTSOCKET socket)
{
    return OptionSetterProxy(socket);
}

}
#endif // INC_SRT_APPCOMMON_H
