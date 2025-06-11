/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/
#ifndef INC_SRT_CHANNEL_H
#define INC_SRT_CHANNEL_H

#include "platform_sys.h"
#include "udt.h"
#include "packet.h"
#include "socketconfig.h"
#include "netinet_any.h"

namespace srt
{

// UDP链路通道管理类
//  - UDP socket管理：创建、绑定、设置socket选项、设置IP TTL、设置IP ToS
//  - 发送、接收UDP数据包
//  - 管理发送/接受缓冲区
class CChannel
{
    // 调用系统API，创建UDP socket
    void createSocket(int family);

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the channel is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".

    // 暂未实现
    std::string CONID() const { return ""; }

    CChannel();
    ~CChannel();

    /// Open a UDP channel.
    /// @param [in] addr The local address that UDP will use.

    // 创建UDP socket并绑定到addr,设置接受/发送缓冲区; 设置TTL / TOS / NONBLOCK / PKT_INFO
    void open(const sockaddr_any& addr);

    // 让系统自动分配一个可用的本地地址，bind并设置接受/发送缓冲区; 设置TTL / TOS / NONBLOCK / PKT_INFO
    void open(int family);

    /// Open a UDP channel based on an existing UDP socket.
    /// @param [in] udpsock UDP socket descriptor.

    // 使用已存在的UDP socket来创建UDP通道，并设置接受/发送缓冲区; 设置TTL / TOS / NONBLOCK / PKT_INFO
    void attach(UDPSOCKET udpsock, const sockaddr_any& adr);

    /// Disconnect and close the UDP entity.

    // 调用系统API 关闭UDP socket
    void close() const;

    /// Get the UDP sending buffer size.
    /// @return Current UDP sending buffer size.

    // 调用系统API，获取发送缓冲区大小
    int getSndBufSize();

    /// Get the UDP receiving buffer size.
    /// @return Current UDP receiving buffer size.

    // 调用系统API，获取接受缓冲区大小
    int getRcvBufSize();

    /// Query the socket address that the channel is using.
    /// @param [out] addr pointer to store the returned socket address.

    // 获取已绑定的本地地址
    void getSockAddr(sockaddr_any& addr) const;

    /// Query the peer side socket address that the channel is connect to.
    /// @param [out] addr pointer to store the returned socket address.

    // 获取对端地址
    void getPeerAddr(sockaddr_any& addr) const;

    /// Send a packet to the given address.
    /// @param [in] addr pointer to the destination address.
    /// @param [in] packet reference to a CPacket entity.
    /// @param [in] src source address to sent on an outgoing packet (if not ANY)
    /// @return Actual size of data sent.

    // 发送数据包到指定地址, src参数没有使用
    int sendto(const sockaddr_any& addr, srt::CPacket& packet, const sockaddr_any& src) const;

    /// Receive a packet from the channel and record the source address.
    /// @param [in] addr pointer to the source address.
    /// @param [in] packet reference to a CPacket entity.
    /// @return Actual size of data received.

    // 接收数据包
    EReadStatus recvfrom(sockaddr_any& addr, srt::CPacket& packet) const;

    // 设置多路复用器参数
    void setConfig(const CSrtMuxerConfig& config);

    // 获取系统套接字属性
    void getSocketOption(int level, int sockoptname, char* pw_dataptr, socklen_t& w_len, int& w_status);

    // 模板函数，获取系统套接字属性的通用实现
    template<class Type>
    Type sockopt(int level, int sockoptname, Type deflt)
    {
        Type retval;
        socklen_t socklen = sizeof retval;
        int status;
        getSocketOption(level, sockoptname, ((char*)&retval), (socklen), (status));
        if (status == -1)
            return deflt;

        return retval;
    }

    /// Get the IP TTL.
    /// @param [in] ttl IP Time To Live.
    /// @return TTL.

    int getIpTTL() const;

    /// Get the IP Type of Service.
    /// @return ToS.

    int getIpToS() const;

#ifdef SRT_ENABLE_BINDTODEVICE
    bool getBind(char* dst, size_t len);
#endif

    int ioctlQuery(int type) const;
    int sockoptQuery(int level, int option) const;

    const sockaddr*     bindAddress() { return m_BindAddr.get(); }
    const sockaddr_any& bindAddressAny() { return m_BindAddr; }

private:

    // 调用系统API，设置接受/发送缓冲区; 设置TTL / TOS / NONBLOCK / PKT_INFO
    void setUDPSockOpt();

private:
    UDPSOCKET m_iSocket; // socket descriptor

    // Mutable because when querying original settings
    // this comprises the cache for extracted values,
    // although the object itself isn't considered modified.

    // 多路复用器
    mutable CSrtMuxerConfig m_mcfg; // Note: ReuseAddr is unused and ineffective.
    sockaddr_any            m_BindAddr;     // bind的本地地址

    // This feature is not enabled on Windows, for now.
    // This is also turned off in case of MinGW
#ifdef SRT_ENABLE_PKTINFO
    bool                    m_bBindMasked; // True if m_BindAddr is INADDR_ANY. Need for quick check.

    // Calculating the required space is extremely tricky, and whereas on most
    // platforms it's possible to define it this way:
    //
    // size_t s = max( CMSG_SPACE(sizeof(in_pktinfo)), CMSG_SPACE(sizeof(in6_pktinfo)) )
    //
    // ...on some platforms however CMSG_SPACE macro can't be resolved as constexpr.
    //
    // This structure is exclusively used to determine the required size for
    // CMSG buffer so that it can be allocated in a solid block with CChannel.
    // NOT TO BE USED to access any data inside the CMSG message.


    // 计算IPv4控制消息缓冲区大小的结构体
    struct CMSGNodeIPv4
    {
        in_pktinfo in4;         // IPv4数据包信息
        size_t extrafill;       // 额外的填充空间
        cmsghdr hdr;            // 控制消息头
    };

    // 计算IPv6控制消息缓冲区大小的结构体
    struct CMSGNodeIPv6
    {
        in6_pktinfo in6;
        size_t extrafill;
        cmsghdr hdr;
    };

    sockaddr_any getTargetAddress(const msghdr& msg) const
    {
        // Loop through IP header messages
        cmsghdr* cmsg;
        for (cmsg = CMSG_FIRSTHDR(&msg);
                cmsg != NULL;
                cmsg = CMSG_NXTHDR(((msghdr*)&msg), cmsg))
        {
            // This should be safe - this packet contains always either
            // IPv4 headers or IPv6 headers.
            if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO)
            {
                in_pktinfo dest_ip;
                memcpy(&dest_ip, CMSG_DATA(cmsg), sizeof(struct in_pktinfo));
                return sockaddr_any(dest_ip.ipi_addr, 0);
            }

            if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO)
            {
                in6_pktinfo dest_ip;
                memcpy(&dest_ip, CMSG_DATA(cmsg), sizeof(struct in6_pktinfo));
                return sockaddr_any(dest_ip.ipi6_addr, 0);
            }
        }

        // Fallback for an error
        return sockaddr_any(m_BindAddr.family());
    }

    // IMPORTANT!!! This function shall be called EXCLUSIVELY just before
    // calling ::sendmsg function. It uses a static buffer to supply data
    // for the call, and it's stated that only one thread is trying to
    // use a CChannel object in sending mode.
    bool setSourceAddress(msghdr& mh, char *buf, const sockaddr_any& adr) const
    {
        // In contrast to an advice followed on the net, there's no case of putting
        // both IPv4 and IPv6 ancillary data, case we could have them. Only one
        // IP version is used and it's the version as found in @a adr, which should
        // be the version used for binding.

        if (adr.family() == AF_INET)
        {
            mh.msg_control = (void *) buf;
            mh.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));

            cmsghdr* cmsg_send = CMSG_FIRSTHDR(&mh);
            cmsg_send->cmsg_level = IPPROTO_IP;
            cmsg_send->cmsg_type = IP_PKTINFO;
            cmsg_send->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
            
            in_pktinfo pktinfo;
            pktinfo.ipi_ifindex = 0;
            pktinfo.ipi_spec_dst = adr.sin.sin_addr;
            memcpy(CMSG_DATA(cmsg_send), &pktinfo, sizeof(in_pktinfo));

            return true;
        }

        if (adr.family() == AF_INET6)
        {
            mh.msg_control = buf;
            mh.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));

            cmsghdr* cmsg_send = CMSG_FIRSTHDR(&mh);
            cmsg_send->cmsg_level = IPPROTO_IPV6;
            cmsg_send->cmsg_type = IPV6_PKTINFO;
            cmsg_send->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));

            in6_pktinfo* pktinfo = (in6_pktinfo*) CMSG_DATA(cmsg_send);
            pktinfo->ipi6_ifindex = 0;
            pktinfo->ipi6_addr = adr.sin6.sin6_addr;

            return true;
        }

        return false;
    }

#endif //SRT_ENABLE_PKTINFO

};

} // namespace srt

#endif
