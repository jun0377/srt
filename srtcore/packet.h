/*
 * SRT - Secure Reliable Transport
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
   Yunhong Gu, last updated 01/02/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_PACKET_H
#define INC_SRT_PACKET_H

#include "udt.h"
#include "common.h"
#include "utilities.h"
#include "netinet_any.h"
#include "packetfilter_api.h"

namespace srt
{

//////////////////////////////////////////////////////////////////////////////
// The purpose of the IOVector class is to proide a platform-independet interface
// to the WSABUF on Windows and iovec on Linux, that can be easilly converted
// to the native structure for use in WSARecvFrom() and recvmsg(...) functions

// 提供一个和平台无关的接口，封装一个ivoec结构，在Windows上是WSABUF，在Linux上是iovec
// 用于网络IO操作中的数据缓冲区
class IOVector
#ifdef _WIN32
    : public WSABUF
#else
    : public iovec
#endif
{
public:
    IOVector() { set(NULL, 0); }

    // 设置数据缓冲区的起始地址和长度
    inline void set(void* buffer, size_t length)
    {
#ifdef _WIN32
        len = (ULONG)length;
        buf = (CHAR*)buffer;
#else
        iov_base = (void*)buffer;
        iov_len  = length;
#endif
    }

    // 获取数据缓冲区的起始地址，注意返回的是一个指针的引用，可以修改数据
    inline char*& dataRef()
    {
#ifdef _WIN32
        return buf;
#else
        return (char*&)iov_base;
#endif
    }

    // 获取数据缓冲区的起始地址，注意返回的是普通指针，只能读取数据
    inline char* data()
    {
#ifdef _WIN32
        return buf;
#else
        return (char*)iov_base;
#endif
    }

    // 获取数据缓冲区的容量
    inline size_t size() const
    {
#ifdef _WIN32
        return (size_t)len;
#else
        return iov_len;
#endif
    }

    // 设置数据缓冲区的容量
    inline void setLength(size_t length)
    {
#ifdef _WIN32
        len = (ULONG)length;
#else
        iov_len = length;
#endif
    }
};

/// To define packets in order in the buffer. This is public due to being used in buffer.
// 表示SRT数据包在消息中的位置和边界信息
enum PacketBoundary
{
    PB_SUBSEQUENT = 0, // 中间的包 00: a packet in the middle of a message, neither the first, not the last.
    PB_LAST       = 1, // 最后一个包 01: last packet of a message
    PB_FIRST      = 2, // 第一个包 10: first packet of a message
    PB_SOLO       = 3, // 当前SRT包是一个完整的消息 11: solo message packet
};

// Breakdown of the PM_SEQNO field in the header:
// 包头的序列号字段

//  C| X X ... X, where:
// 最高位(31) 用作控制标志位
typedef Bits<31> SEQNO_CONTROL;

//  1|T T T T T T T T T T T T T T T|E E...E
// 当最高位为1，即表示一个控制报文时 bit[30:16] 表示消息类型
typedef Bits<30, 16> SEQNO_MSGTYPE;
// bid[15:0] 表示扩展类型
typedef Bits<15, 0>  SEQNO_EXTTYPE;

//  0|S S ... S
// 当最高位为0，即表示一个普通数据报文时，bit[30:0] 用作数据包的序列号
typedef Bits<30, 0> SEQNO_VALUE;

// This bit cannot be used by SEQNO anyway, so it's additionally used
// in LOSSREPORT data specification to define that this value is the
// BEGIN value for a SEQNO range (to distinguish it from a SOLO loss SEQNO value).

// 丢包报告中序列号范围的起始值
const int32_t LOSSDATA_SEQNO_RANGE_FIRST = SEQNO_CONTROL::mask;

// Just cosmetics for readability.
// LOSSDATA_SEQNO_RANGE_LAST-丢包报告中序列号范围的结束值; LOSSDATA_SEQNO_SOLO-标记单个丢包
const int32_t LOSSDATA_SEQNO_RANGE_LAST = 0, LOSSDATA_SEQNO_SOLO = 0;

// 根据消息类型创建控制包的序列号
inline int32_t CreateControlSeqNo(UDTMessageType type)
{
    return SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(uint32_t(type));
}

// 用于创建扩展控制包的序列号
inline int32_t CreateControlExtSeqNo(int exttype)
{
    return SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(size_t(UMSG_EXT)) | SEQNO_EXTTYPE::wrap(exttype);
}

// MSGNO breakdown: B B|O|K K|R|M M M M M M M M M M...M
typedef Bits<31, 30> MSGNO_PACKET_BOUNDARY;         // BB - 消息边界信息
typedef Bits<29>     MSGNO_PACKET_INORDER;          // O - 是否需要按序交付
typedef Bits<28, 27> MSGNO_ENCKEYSPEC;              // KK - 加密密钥规格 00=不加密，01=偶数密钥，10=奇数密钥            
#if 1 // can block rexmit flag
// New bit breakdown - rexmit flag supported.
typedef Bits<26>    MSGNO_REXMIT;                   // R - 标记是否为重传包
typedef Bits<25, 0> MSGNO_SEQ;                      // 26位序列号
// Old bit breakdown - no rexmit flag
typedef Bits<26, 0> MSGNO_SEQ_OLD;                  // 旧版本的27位序列号，不支持重传标记
// This symbol is for older SRT version, where the peer does not support the MSGNO_REXMIT flag.
// The message should be extracted as PMASK_MSGNO_SEQ, if REXMIT is supported, and PMASK_MSGNO_SEQ_OLD otherwise.

// PACKET_SND_NORMAL - 普通数据包; PACKET_SND_REXMIT - 重传数据包
const uint32_t PACKET_SND_NORMAL = 0, PACKET_SND_REXMIT = MSGNO_REXMIT::mask;
// 消息序列号的最大值
const int      MSGNO_SEQ_MAX = MSGNO_SEQ::mask;

#else
// Old bit breakdown - no rexmit flag
// 旧的消息序列号
typedef Bits<26, 0> MSGNO_SEQ;
#endif

// 消息号
typedef RollNumber<MSGNO_SEQ::size - 1, 1> MsgNo;

// constexpr in C++11 ! 将枚举值转换为消息号中边界位
inline int32_t PacketBoundaryBits(PacketBoundary o)
{
    return MSGNO_PACKET_BOUNDARY::wrap(int32_t(o));
}

// 加密密钥规格
enum EncryptionKeySpec
{
    EK_NOENC = 0,   // 不加密
    EK_EVEN  = 1,   // 使用偶数密钥
    EK_ODD   = 2    // 使用奇数密钥
};

// 加密状态
enum EncryptionStatus
{
    ENCS_CLEAR  = 0,    // 正常
    ENCS_FAILED = -1,   // 加密失败
    ENCS_NOTSUP = -2    // 不支持加密
};

// 加密密钥掩码
const int32_t  PMASK_MSGNO_ENCKEYSPEC = MSGNO_ENCKEYSPEC::mask;
// 将加密规格转换为消息号中对应的位值
inline int32_t EncryptionKeyBits(EncryptionKeySpec f)
{
    return MSGNO_ENCKEYSPEC::wrap(int32_t(f));
}

// 从消息号中提取加密密钥规格
inline EncryptionKeySpec GetEncryptionKeySpec(int32_t msgno)
{
    return EncryptionKeySpec(MSGNO_ENCKEYSPEC::unwrap(msgno));
}

// 探测包的掩码常量，限制了探测包的序列号范围在[15:0]之间
const int32_t PUMASK_SEQNO_PROBE = 0xF;

// 将消息字段转换为可读的字符串描述
std::string PacketMessageFlagStr(uint32_t msgno_field);

class CPacket
{
    friend class CChannel;
    friend class CSndQueue;
    friend class CRcvQueue;

public:
    CPacket();
    ~CPacket();

    // 在堆上为数据域分配缓冲区空间，并没有分配头部域空间
    void allocate(size_t size);
    // 释放数据域的堆内存
    void deallocate();

    /// Get the payload or the control information field length.
    /// @return the payload or the control information field length.
    
    // 获取数据域容量
    size_t getLength() const;

    /// Set the payload or the control information field length.
    /// @param len [in] the payload or the control information field length.
    
    // 设置数据域的容量
    void setLength(size_t len);

    /// Set the payload or the control information field length.
    /// @param len [in] the payload or the control information field length.
    /// @param cap [in] capacity (if known).

    // 同时设置数据域的负载长度和容量
    void setLength(size_t len, size_t cap);

    /// Pack a Control packet.
    /// @param pkttype [in] packet type filed.
    /// @param lparam [in] pointer to the first data structure, explained by the packet type.
    /// @param rparam [in] pointer to the second data structure, explained by the packet type.
    /// @param size [in] size of rparam, in number of bytes;

    // 封装一个控制包: 握手包 / 心跳包 / 确认包 / 丢包报告包 / 拥塞警告 / 关闭丽娜姐 / 主动丢弃 / 对端错误 / 对ACK的确认
    void pack(UDTMessageType pkttype, const int32_t* lparam = NULL, void* rparam = NULL, size_t size = 0);

    /// Read the packet vector.
    /// @return Pointer to the packet vector.

    // 获取数据包存储的地址, 包括头部和数据部分
    IOVector* getPacketVector();

    // 获取数据包的头部信息
    uint32_t* getHeader() { return m_nHeader; }

    /// Read the packet type.
    /// @return packet type filed (000 ~ 111).
    
    // 获取控制包类型
    UDTMessageType getType() const;

    // 判断是否是指定类型的控制包
    bool isControl(UDTMessageType type) const { return isControl() && type == getType(); }

    // 判断是否是一个控制包
    bool isControl() const { return 0 != SEQNO_CONTROL::unwrap(m_nHeader[SRT_PH_SEQNO]); }

    // 设置控制包的类型
    void setControl(UDTMessageType type) { m_nHeader[SRT_PH_SEQNO] = SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(type); }

    /// Read the extended packet type.
    /// @return extended packet type filed (0x000 ~ 0xFFF).

    // 获取控制包的扩展类型
    int getExtendedType() const;

    /// Read the ACK-2 seq. no.
    /// @return packet header field (bit 16~31).

    // 获取 ACK-2 报文的消息号
    int32_t getAckSeqNo() const;

    uint16_t getControlFlags() const;

    // Note: this will return a "singular" value, if the packet
    // contains the control message
    int32_t getSeqNo() const { return m_nHeader[SRT_PH_SEQNO]; }

    /// Read the message boundary flag bit.
    /// @return packet header field [1] (bit 0~1).
    PacketBoundary getMsgBoundary() const;

    /// Read the message inorder delivery flag bit.
    /// @return packet header field [1] (bit 2).
    bool getMsgOrderFlag() const;

    /// Read the rexmit flag (true if the packet was sent due to retransmission).
    /// If the peer does not support retransmission flag, the current agent cannot use it as well
    /// (because the peer will understand this bit as a part of MSGNO field).
    bool getRexmitFlag() const;

    void setRexmitFlag(bool bRexmit);

    /// Read the message sequence number.
    /// @return packet header field [1]
    int32_t getMsgSeq(bool has_rexmit = true) const;

    /// Read the message crypto key bits.
    /// @return packet header field [1] (bit 3~4).
    EncryptionKeySpec getMsgCryptoFlags() const;

    void setMsgCryptoFlags(EncryptionKeySpec spec);

    /// Read the message time stamp.
    /// @return packet header field [2] (bit 0~31, bit 0-26 if SRT_DEBUG_TSBPD_WRAP).
    uint32_t getMsgTimeStamp() const;

    sockaddr_any udpDestAddr() const { return m_DestAddr; }

#ifdef SRT_DEBUG_TSBPD_WRAP                           // Receiver
    static const uint32_t MAX_TIMESTAMP = 0x07FFFFFF; // 27 bit fast wraparound for tests (~2m15s)
#else
    static const uint32_t MAX_TIMESTAMP = 0xFFFFFFFF; // Full 32 bit (01h11m35s)
#endif

protected:
    static const uint32_t TIMESTAMP_MASK = MAX_TIMESTAMP; // this value to be also used as a mask
public:
    /// Clone this packet.
    /// @return Pointer to the new packet.
    CPacket* clone() const;

    enum PacketVectorFields
    {
        PV_HEADER = 0,  // 头部域索引
        PV_DATA   = 1,  // 数据域索引

        PV_SIZE = 2
    };

public:
    /// @brief Convert the packet inline to a network byte order (Little-endian).
    void toNetworkByteOrder();
	/// @brief Convert the packet inline to a host byte order.
    void toHostByteOrder();

protected:
    // DynamicStruct is the same as array of given type and size, just it
    // enforces that you index it using a symbol from symbolic enum type, not by a bare integer.

    // 128 bits header
    typedef DynamicStruct<uint32_t, SRT_PH_E_SIZE, SrtPktHeaderFields> HEADER_TYPE;
    HEADER_TYPE                                                        m_nHeader; //< The 128-bit header field

    // 存储空间，包括头部和数据： m_PacketVector[0]存储头部；m_PacketVector[1]存储数据
    IOVector m_PacketVector[PV_SIZE]; //< The two-dimensional vector of an SRT packet [header, data]

    int32_t m_extra_pad;        // 额外填充字节，用于内存对齐
    bool    m_data_owned;       // 数据缓冲区的所有权：true，表示数据缓冲区由当前包对象拥有，在析构时需要释放; false，表示数据缓冲区由外部管理，析构时不需要释放
    sockaddr_any m_DestAddr;    // 数据包的目的地址
    size_t  m_zCapacity;        // 数据缓冲区的容量，注意：是容量大小，并不是实际的数据长度

protected:
    CPacket& operator=(const CPacket&);
    CPacket(const CPacket&);

public:
    char*&   m_pcData;     // alias: payload (data packet) / control information fields (control packet)

    SRTU_PROPERTY_RO(SRTSOCKET, id, SRTSOCKET(m_nHeader[SRT_PH_ID]));
    SRTU_PROPERTY_WO_ARG(SRTSOCKET, id, m_nHeader[SRT_PH_ID] = int32_t(arg));

    SRTU_PROPERTY_RW(int32_t, seqno, m_nHeader[SRT_PH_SEQNO]);
    SRTU_PROPERTY_RW(int32_t, msgflags, m_nHeader[SRT_PH_MSGNO]);
    SRTU_PROPERTY_RW(int32_t, timestamp, m_nHeader[SRT_PH_TIMESTAMP]);

    // Experimental: sometimes these references don't work!
    char* getData();
    char* release();

    static const size_t HDR_SIZE = sizeof(HEADER_TYPE); // packet header size = SRT_PH_E_SIZE * sizeof(uint32_t)

    // Can also be calculated as: sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr).
    static const size_t UDP_HDR_SIZE = 28; // 20 bytes IPv4 + 8 bytes of UDP { u16 sport, dport, len, csum }.

    static const size_t SRT_DATA_HDR_SIZE = UDP_HDR_SIZE + HDR_SIZE;

    // Maximum transmission unit size. 1500 in case of Ethernet II (RFC 1191).
    static const size_t ETH_MAX_MTU_SIZE = 1500;

    // Maximum payload size of an SRT packet.
    static const size_t SRT_MAX_PAYLOAD_SIZE = ETH_MAX_MTU_SIZE - SRT_DATA_HDR_SIZE;

    // Packet interface
    char*       data() { return m_pcData; }
    const char* data() const { return m_pcData; }
    size_t      size() const { return getLength(); }
    size_t      capacity() const { return m_zCapacity; }
    void        setCapacity(size_t cap) { m_zCapacity = cap; }
    uint32_t    header(SrtPktHeaderFields field) const { return m_nHeader[field]; }

#if ENABLE_LOGGING
    std::string MessageFlagStr() { return PacketMessageFlagStr(m_nHeader[SRT_PH_MSGNO]); }
    std::string Info();
#else
    std::string           MessageFlagStr() { return std::string(); }
    std::string           Info() { return std::string(); }
#endif
};

} // namespace srt

#endif
