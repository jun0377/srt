/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_URL_PARSER_H
#define INC_SRT_URL_PARSER_H

#include <string>
#include <map>
#include <cstdlib>
#include "utilities.h"


//++
// UriParser
//--

class UriParser
{
// Construction
public:

    enum DefaultExpect { EXPECT_FILE, EXPECT_HOST };        // URI对应的资源是文件还是网络
    enum Type
    {
        UNKNOWN, FILE, UDP, TCP, SRT, RTMP, HTTP, RTP       // 各种类型的URI资源
    };

    UriParser(const std::string& strUrl, DefaultExpect exp = EXPECT_FILE);
    UriParser(): m_uriType(UNKNOWN) {}
    virtual ~UriParser(void);

    // Some predefined types
    Type type() const;          // URI类型: FILE, UDP, TCP, SRT, RTMP, HTTP, RTP

    typedef MapProxy<std::string, std::string> ParamProxy;

// Operations
public:
    std::string uri() const { return m_origUri; }   
    std::string proto() const;                      
    std::string scheme() const { return proto(); }  
    std::string host() const;                       
    std::string port() const;                      
    unsigned short int portno() const;  // 确保端口号在1~65535之间
    std::string hostport() const { return host() + ":" + port(); }  // 拼接主机名:端口号
    std::string path() const;
    std::string queryValue(const std::string& strKey) const;
    std::string makeUri();              //  URI序列化: proto://host:port/path?key1=value1&key2=value2...
    ParamProxy operator[](const std::string& key) { return ParamProxy(m_mapQuery, key); }   // 下标访问map
    const std::map<std::string, std::string>& parameters() const { return m_mapQuery; }
    typedef std::map<std::string, std::string>::const_iterator query_it;

private:
    void Parse(const std::string& strUrl, DefaultExpect);   // 解析URI

// Overridables
public:

// Overrides
public:

// Data
private:
    std::string m_origUri;      // URI
    std::string m_proto;        // 协议
    std::string m_host;         // 主机名
    std::string m_port;         // 端口
    std::string m_path;         // 路径
    Type m_uriType;             // URI类型： FILE, UDP, TCP, SRT, RTMP, HTTP, RTP
    DefaultExpect m_expect;     // URI资源类型：文件/网络

    std::map<std::string, std::string> m_mapQuery;  // 存储URI查询参数
};

//#define TEST1 1

#endif // INC_SRT_URL_PARSER_H
