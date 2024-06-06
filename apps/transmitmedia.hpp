/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_COMMON_TRANSMITMEDIA_HPP
#define INC_SRT_COMMON_TRANSMITMEDIA_HPP

#include <string>
#include <map>
#include <stdexcept>

#include "transmitbase.hpp"
#include <udt.h> // Needs access to CUDTException

using namespace std;

// Trial version of an exception. Try to implement later an official
// interruption mechanism in SRT using this.

struct TransmissionError: public std::runtime_error
{
    TransmissionError(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

// SRT基础类，封装了SRT协议的通用功能和属性
class SrtCommon
{
protected:

    // 数据传输方向，true表示发送数据，false表示接收数据
    bool m_output_direction = false; //< Defines which of SND or RCV option variant should be used, also to set SRT_SENDER for output
    // 发送/接收超时时间，发送时表示发送超时，接收时表示接收超时
    int m_timeout = 0; //< enforces using SRTO_SNDTIMEO or SRTO_RCVTIMEO, depending on @a m_output_direction
    // TSBPD模式 Q:TSBPD模式是什么模式
    bool m_tsbpdmode = true;
    // 发送使用的端口
    int m_outgoing_port = 0;
    string m_mode;
    string m_adapter;
    // 存放URI中的选项
    map<string, string> m_options; // All other options, as provided in the URI
    // 和对端建立连接使用的sockfd
    SRTSOCKET m_sock = SRT_INVALID_SOCK;
    // 监听sockfd
    SRTSOCKET m_bindsock = SRT_INVALID_SOCK;
    // 判断socket是否可用
    bool IsUsable() { SRT_SOCKSTATUS st = srt_getsockstate(m_sock); return st > SRTS_INIT && st < SRTS_BROKEN; }
    // 判断socket是否已断开
    bool IsBroken() { return srt_getsockstate(m_sock) > SRTS_CONNECTED; }

public:
    // 初始化参数
    void InitParameters(string host, map<string,string> par);
    // 准备监听

    void PrepareListener(string host, int port, int backlog);
    // 从另一个SrtCommon对象中获取属性
    void StealFrom(SrtCommon& src);
    // 接受新客户端
    bool AcceptNewClient();
    // 获取SRT socket
    SRTSOCKET Socket() const { return m_sock; }
    // 获取绑定的socket
    SRTSOCKET Listener() const { return m_bindsock; }
    // 关闭socket
    void Close();

protected:
    // 处理错误
    void Error(string src);
    // 初始化
    void Init(string host, int port, map<string,string> par, bool dir_output);

    // 配置后的设置
    virtual int ConfigurePost(SRTSOCKET sock);
    // 配置前的设置
    virtual int ConfigurePre(SRTSOCKET sock);

    // 打开客户端连接
    void OpenClient(string host, int port);
    // 准备客户端
    void PrepareClient();
    // 设置适配器
    void SetupAdapter(const std::string& host, int port);
    // 连接客户端
    void ConnectClient(string host, int port);

    // 打开服务器
    void OpenServer(string host, int port)
    {
        PrepareListener(host, port, 1);
    }

    // 打开Rendezvous连接　Q:什么叫rendezvous连接?
    void OpenRendezvous(string adapter, string host, int port);

    // 虚析构
    virtual ~SrtCommon();
};


class SrtSource: public Source, public SrtCommon
{
    std::string hostport_copy;
public:

    SrtSource(std::string host, int port, const std::map<std::string,std::string>& par);
    SrtSource()
    {
        // Do nothing - create just to prepare for use
    }

    int Read(size_t chunk, MediaPacket& pkt, ostream& out_stats = cout) override;

    /*
       In this form this isn't needed.
       Unblock if any extra settings have to be made.
    virtual int ConfigurePre(UDTSOCKET sock) override
    {
        int result = SrtCommon::ConfigurePre(sock);
        if ( result == -1 )
            return result;
        return 0;
    }
    */

    bool IsOpen() override { return IsUsable(); }
    bool End() override { return IsBroken(); }

    SRTSOCKET GetSRTSocket() const override
    { 
        SRTSOCKET socket = SrtCommon::Socket();
        if (socket == SRT_INVALID_SOCK)
            socket = SrtCommon::Listener();
        return socket;
    }

    bool AcceptNewClient() override { return SrtCommon::AcceptNewClient(); }
};

class SrtTarget: public Target, public SrtCommon
{
public:

    SrtTarget(std::string host, int port, const std::map<std::string,std::string>& par)
    {
        Init(host, port, par, true);
    }

    SrtTarget() {}

    int ConfigurePre(SRTSOCKET sock) override;
    int Write(const char* data, size_t size, int64_t src_time, ostream &out_stats = cout) override;
    bool IsOpen() override { return IsUsable(); }
    bool Broken() override { return IsBroken(); }

    size_t Still() override
    {
        size_t bytes;
        int st = srt_getsndbuffer(m_sock, nullptr, &bytes);
        if (st == -1)
            return 0;
        return bytes;
    }

    SRTSOCKET GetSRTSocket() const override
    { 
        SRTSOCKET socket = SrtCommon::Socket();
        if (socket == SRT_INVALID_SOCK)
            socket = SrtCommon::Listener();
        return socket;
    }
    bool AcceptNewClient() override { return SrtCommon::AcceptNewClient(); }
};


// This class is used when we don't know yet whether the given URI
// designates an effective listener or caller. So we create it, initialize,
// then we know what mode we'll be using.
//
// When caller, then we will do connect() using this object, then clone out
// a new object - of a direction specific class - which will steal the socket
// from this one and then roll the data. After this, this object is ready
// to connect again, and will create its own socket for that occasion, and
// the whole procedure repeats.
//
// When listener, then this object will be doing accept() and with every
// successful acceptation it will clone out a new object - of a direction
// specific class - which will steal just the connection socket from this
// object. This object will still live on and accept new connections and
// so on.
class SrtModel: public SrtCommon
{
public:
    bool is_caller = false;
    string m_host;
    int m_port = 0;

    SrtModel(string host, int port, map<string,string> par);
    void Establish(std::string& name);
};



#endif
