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
Copyright (c) 2001 - 2010, The Board of Trustees of the University of Illinois.
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
    Yunhong Gu, last updated 09/28/2010
modified by
    Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_API_H
#define INC_SRT_API_H

#include <map>
#include <vector>
#include <string>
#include "netinet_any.h"
#include "udt.h"
#include "packet.h"
#include "queue.h"
#include "cache.h"
#include "epoll.h"
#include "handshake.h"
#include "core.h"
#if ENABLE_BONDING
#include "group.h"
#endif

// Please refer to structure and locking information provided in the
// docs/dev/low-level-info.md document.

namespace srt
{

class CUDT;

// class CUDTSocket封装了 SRT 连接的核心操作和功能
// 这个类的作用主要包括管理 SRT 连接、发送和接收数据、设置和获取连接参数、以及处理连接中的各种事件和状态
class CUDTSocket
{
public:
    CUDTSocket()                    // 默认构造函数
        : m_Status(SRTS_INIT)       // 初始化状态为初始状态
        , m_SocketID(0)             // 初始化套接字ID为0
        , m_ListenSocket(0)         // 初始化监听套接字为0
        , m_PeerID(0)               // 初始化对端ID为0
#if ENABLE_BONDING
        , m_GroupMemberData()       // 如果启用绑定，初始化组成员数据
        , m_GroupOf()               // 初始化所属组
#endif
        , m_iISN(0)                 // 初始化初始序列号为0
        , m_UDT(this)               // 初始化UDT实例
        , m_AcceptCond()            // 初始化接受条件
        , m_AcceptLock()            // 初始化接受锁
        , m_uiBackLog(0)            // 初始化备份队列长度为0
        , m_iMuxID(-1)              // 初始化多路复用ID为-1
    {
        construct();                // 调用构造函数完成进一步初始化
    }

    CUDTSocket(const CUDTSocket& ancestor)      // 拷贝构造函数
        : m_Status(SRTS_INIT)
        , m_SocketID(0)
        , m_ListenSocket(0)
        , m_PeerID(0)
#if ENABLE_BONDING
        , m_GroupMemberData()
        , m_GroupOf()
#endif
        , m_iISN(0)
        , m_UDT(this, ancestor.m_UDT)
        , m_AcceptCond()
        , m_AcceptLock()
        , m_uiBackLog(0)
        , m_iMuxID(-1)
    {
        construct();
    }

    ~CUDTSocket();

    void construct();

    // 用于指示特定的变量受某个锁的保护，给静态代码分析工具使用
    SRT_ATTR_GUARDED_BY(m_ControlLock)
    // 当前套接字状态，
    sync::atomic<SRT_SOCKSTATUS> m_Status; //< current socket state

    /// Time when the socket is closed.
    /// When the socket is closed, it is not removed immediately from the list
    /// of sockets in order to prevent other methods from accessing invalid address.
    /// A timer is started and the socket will be removed after approximately
    /// 1 second (see CUDTUnited::checkBrokenSockets()).
    /*  当套接字被关闭时，为了防止其他方法仍在使用该套接字，它不会立即从套接字列表中移除。 
        会启动一个计时器，在大约1秒后（请参阅 CUDTUnited::checkBrokenSockets() 函数）才会真正删除该套接字
    */
    sync::steady_clock::time_point m_tsClosureTimeStamp;

    // 本地地址，包括IP和端口号
    sockaddr_any m_SelfAddr; //< local address of the socket
    // 对端地址，包括IP和端口号
    sockaddr_any m_PeerAddr; //< peer address of the socket

    // sockfd 
    SRTSOCKET m_SocketID;     //< socket ID
    // 监听套接字，0表示是一个非监听套接字
    SRTSOCKET m_ListenSocket; //< ID of the listener socket; 0 means this is an independent socket
    // 对端套接字
    SRTSOCKET m_PeerID; //< peer socket ID
#if ENABLE_BONDING
    groups::SocketData* m_GroupMemberData; //< Pointer to group member data, or NULL if not a group member
    CUDTGroup*          m_GroupOf;         //< Group this socket is a member of, or NULL if it isn't
#endif

    // 初始序列号，用于区分来自相同IP地址和端口的不同连接
    int32_t m_iISN; //< initial sequence number, used to tell different connection from same IP:port

private:
    // 内部SRT套接字逻辑
    CUDT m_UDT; //< internal SRT socket logic

public:
    // 等待accept的套接字集合
    std::set<SRTSOCKET> m_QueuedSockets; //< set of connections waiting for accept()

    // 下面这两个变量用来实现同步操作
    sync::Condition m_AcceptCond; //< used to block "accept" call
    sync::Mutex     m_AcceptLock; //< mutex associated to m_AcceptCond

    // 队列中等待 accept() 处理的最大连接数
    unsigned int m_uiBackLog; //< maximum number of connections in queue

    // XXX A refactoring might be needed here.

    // There are no reasons found why the socket can't contain a list iterator to a
    // multiplexer INSTEAD of m_iMuxID. There's no danger in this solution because
    // the multiplexer is never deleted until there's at least one socket using it.
    //
    // The multiplexer may even physically be contained in the CUDTUnited object,
    // just track the multiple users of it (the listener and the accepted sockets).
    // When deleting, you simply "unsubscribe" yourself from the multiplexer, which
    // will unref it and remove the list element by the iterator kept by the
    // socket.
    int m_iMuxID; //< multiplexer ID

    // 同步互斥锁：用于在执行控制API（如 bind、listen 和 connect）时独占性地锁定该套接字
    sync::Mutex m_ControlLock; //< lock this socket exclusively for control APIs: bind/listen/connect

    CUDT&       core() { return m_UDT; }    // 返回CUDT对象的引用。这允许调用者直接使用CUDT对象
    const CUDT& core() const { return m_UDT; }// 返回CUDT对象的常引用，不允许调用者修改CUDT对象

    // 静态方法：返回一个64位整数，该整数由SRT socket ID和序列号组合而成
    static int64_t getPeerSpec(SRTSOCKET id, int32_t isn) { return (int64_t(id) << 30) + isn; }
    
    int64_t        getPeerSpec() { return getPeerSpec(m_PeerID, m_iISN); }

    // 获取sockfd状态
    SRT_SOCKSTATUS getStatus();

    // 用于中断或关闭SRT连接，线程安全
    void breakSocket_LOCKED();

    // 将 SRT 套接字标记为已关闭状态，以便释放相关资源并防止之后的操作使用该套接字
    void setClosed();

    // This is necessary to be called from the group before the group clears
    // the connection with the socket. As for managed groups (and there are
    // currently no other group types), a socket disconnected from the group
    // is no longer usable.
    void setClosing()
    {
        core().m_bClosing = true;
    }

    /// 这个操作与setClosed相同，另外会将m_bBroken设置为true。
    /// 在这种状态下，套接字仍然可以被读取，以便读取接收缓冲区中剩余的数据，
    /// 但不再发送任何新数据。
    void setBrokenClosed();
    void removeFromGroup(bool broken);

    // Instrumentally used by select() and also required for non-blocking
    // mode check in groups
    // 用户select()函数或非阻塞sockfd
    bool readReady();
    bool writeReady() const;
    bool broken() const;

private:
    CUDTSocket& operator=(const CUDTSocket&);
};

////////////////////////////////////////////////////////////////////////////////

// 用于管理和操作SRT套接字
class CUDTUnited
{
    friend class CUDT;
    friend class CUDTGroup;
    friend class CRendezvousQueue;
    friend class CCryptoControl;

public:
    CUDTUnited();
    ~CUDTUnited();

    // Public constants
    static const int32_t MAX_SOCKET_VAL = SRTGROUP_MASK - 1; // maximum value for a regular socket

public:
    enum ErrorHandling
    {
        ERH_RETURN,
        ERH_THROW,
        ERH_ABORT
    };
    static std::string CONID(SRTSOCKET sock);

    /// initialize the UDT library.
    /// @return 0 if success, otherwise -1 is returned.
    // 初始化UDT库
    int startup();

    /// release the UDT library.
    /// @return 0 if success, otherwise -1 is returned.
    int cleanup();

    /// Create a new UDT socket.
    /// @param [out] pps Variable (optional) to which the new socket will be written, if succeeded
    /// @return The new UDT socket ID, or INVALID_SOCK.
    SRTSOCKET newSocket(CUDTSocket** pps = NULL);

    /// Create (listener-side) a new socket associated with the incoming connection request.
    /// @param [in] listen the listening socket ID.
    /// @param [in] peer peer address.
    /// @param [in,out] hs handshake information from peer side (in), negotiated value (out);
    /// @param [out] w_error error code in case of failure.
    /// @param [out] w_acpu reference to the existing associated socket if already exists.
    /// @return  1: if the new connection was successfully created (accepted), @a w_acpu is NULL;
    ///          0: the connection already exists (reference to the corresponding socket is returned in @a w_acpu).
    ///         -1: The connection processing failed due to memory alloation error, exceeding listener's backlog,
    ///             any error propagated from CUDT::open and CUDT::acceptAndRespond.
    int newConnection(const SRTSOCKET     listen,
                      const sockaddr_any& peer,
                      const CPacket&      hspkt,
                      CHandShake&         w_hs,
                      int&                w_error,
                      CUDT*&              w_acpu);

    int installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq);
    int installConnectHook(const SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq);

    /// Check the status of the UDT socket.
    /// @param [in] u the UDT socket ID.
    /// @return UDT socket status, or NONEXIST if not found.
    SRT_SOCKSTATUS getStatus(const SRTSOCKET u);

    // socket APIs

    int       bind(CUDTSocket* u, const sockaddr_any& name);
    int       bind(CUDTSocket* u, UDPSOCKET udpsock);
    int       listen(const SRTSOCKET u, int backlog);
    SRTSOCKET accept(const SRTSOCKET listen, sockaddr* addr, int* addrlen);
    SRTSOCKET accept_bond(const SRTSOCKET listeners[], int lsize, int64_t msTimeOut);
    int       connect(SRTSOCKET u, const sockaddr* srcname, const sockaddr* tarname, int tarlen);
    int       connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
    int       connectIn(CUDTSocket* s, const sockaddr_any& target, int32_t forced_isn);
#if ENABLE_BONDING
    int groupConnect(CUDTGroup* g, SRT_SOCKGROUPCONFIG targets[], int arraysize);
    int singleMemberConnect(CUDTGroup* g, SRT_SOCKGROUPCONFIG* target);
#endif
    int  close(const SRTSOCKET u);
    int  close(CUDTSocket* s);
    void getpeername(const SRTSOCKET u, sockaddr* name, int* namelen);
    void getsockname(const SRTSOCKET u, sockaddr* name, int* namelen);
    int  select(UDT::UDSET* readfds, UDT::UDSET* writefds, UDT::UDSET* exceptfds, const timeval* timeout);
    int  selectEx(const std::vector<SRTSOCKET>& fds,
                  std::vector<SRTSOCKET>*       readfds,
                  std::vector<SRTSOCKET>*       writefds,
                  std::vector<SRTSOCKET>*       exceptfds,
                  int64_t                       msTimeOut);
    int  epoll_create();
    int  epoll_clear_usocks(int eid);
    int  epoll_add_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    int  epoll_add_usock_INTERNAL(const int eid, CUDTSocket* s, const int* events);
    int  epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    int  epoll_remove_usock(const int eid, const SRTSOCKET u);
    template <class EntityType>
    int epoll_remove_entity(const int eid, EntityType* ent);
    int epoll_remove_socket_INTERNAL(const int eid, CUDTSocket* ent);
#if ENABLE_BONDING
    int epoll_remove_group_INTERNAL(const int eid, CUDTGroup* ent);
#endif
    int     epoll_remove_ssock(const int eid, const SYSSOCKET s);
    int     epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    int     epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
    int32_t epoll_set(const int eid, int32_t flags);
    int     epoll_release(const int eid);

#if ENABLE_BONDING
    // [[using locked(m_GlobControlLock)]]
    CUDTGroup& addGroup(SRTSOCKET id, SRT_GROUP_TYPE type)
    {
        // This only ensures that the element exists.
        // If the element was newly added, it will be NULL.
        CUDTGroup*& g = m_Groups[id];
        if (!g)
        {
            // This is a reference to the cell, so it will
            // rewrite it into the map.
            g = new CUDTGroup(type);
        }

        // Now we are sure that g is not NULL,
        // and persistence of this object is in the map.
        // The reference to the object can be safely returned here.
        return *g;
    }

    void deleteGroup(CUDTGroup* g);
    void deleteGroup_LOCKED(CUDTGroup* g);

    // [[using locked(m_GlobControlLock)]]
    CUDTGroup* findPeerGroup_LOCKED(SRTSOCKET peergroup)
    {
        for (groups_t::iterator i = m_Groups.begin(); i != m_Groups.end(); ++i)
        {
            if (i->second->peerid() == peergroup)
                return i->second;
        }
        return NULL;
    }
#endif

    CEPoll& epoll_ref() { return m_EPoll; }

private:
    /// Generates a new socket ID. This function starts from a randomly
    /// generated value (at initialization time) and goes backward with
    /// with next calls. The possible values come from the range without
    /// the SRTGROUP_MASK bit, and the group bit is set when the ID is
    /// generated for groups. It is also internally checked if the
    /// newly generated ID isn't already used by an existing socket or group.
    ///
    /// Socket ID value range.
    /// - [0]: reserved for handshake procedure. If the destination Socket ID is 0
    ///   (destination Socket ID unknown) the packet will be sent to the listening socket
    ///   or to a socket that is in the rendezvous connection phase.
    /// - [1; 2 ^ 30): single socket ID range.
    /// - (2 ^ 30; 2 ^ 31): group socket ID range. Effectively any positive number
    ///   from [1; 2 ^ 30) with bit 30 set to 1. Bit 31 is zero.
    /// The most significant bit 31 (sign bit) is left unused so that checking for a value <= 0 identifies an invalid
    /// socket ID.
    ///
    /// @param group The socket id should be for socket group.
    /// @return The new socket ID.
    /// @throw CUDTException if after rolling over all possible ID values nothing can be returned
    SRTSOCKET generateSocketID(bool group = false);

private:
    // 存储所有socket结构体的MAP
    typedef std::map<SRTSOCKET, CUDTSocket*> sockets_t; // stores all the socket structures
    sockets_t                                m_Sockets;

#if ENABLE_BONDING
    typedef std::map<SRTSOCKET, CUDTGroup*> groups_t;
    groups_t                                m_Groups;
#endif

    // 用于同步UDT API
    sync::Mutex m_GlobControlLock; // used to synchronize UDT API
    // 用于同步sockfd生成
    sync::Mutex m_IDLock; // used to synchronize ID generation

    // 用于生成一个新的socketfd的种子
    SRTSOCKET m_SocketIDGenerator;      // seed to generate a new unique socket ID
    // 用于记录第一个生成的socket id
    SRTSOCKET m_SocketIDGenerator_init; // Keeps track of the very first one

    // 用于记录来自对等方的套接字，以避免重复的连接请求，int64_t = (套接字ID << 30) + ISN
    std::map<int64_t, std::set<SRTSOCKET> >
        m_PeerRec; // record sockets from peers to avoid repeated connection request, int64_t = (socker_id << 30) + isn

private:
    friend struct FLookupSocketWithEvent_LOCKED;

    // 给定套接字标识符，查找并返回指向指定套接字对象的指针
    CUDTSocket* locateSocket(SRTSOCKET u, ErrorHandling erh = ERH_RETURN);
    // This function does the same as locateSocket, except that:
    // - lock on m_GlobControlLock is expected (so that you don't unlock between finding and using)
    // - only return NULL if not found
    // - 在查找和实际使用套接字之间，不会发生解锁操作，从而确保了操作的原子性和安全性
    // -  如果没有找到指定的套接字，此函数仅返回NULL
    CUDTSocket* locateSocket_LOCKED(SRTSOCKET u);
    // 
    CUDTSocket* locatePeer(const sockaddr_any& peer, const SRTSOCKET id, int32_t isn);

#if ENABLE_BONDING
    CUDTGroup* locateAcquireGroup(SRTSOCKET u, ErrorHandling erh = ERH_RETURN);
    CUDTGroup* acquireSocketsGroup(CUDTSocket* s);

    struct GroupKeeper
    {
        CUDTGroup* group;

        // This is intended for API functions to lock the group's existence
        // for the lifetime of their call.
        GroupKeeper(CUDTUnited& glob, SRTSOCKET id, ErrorHandling erh) { group = glob.locateAcquireGroup(id, erh); }

        // This is intended for TSBPD thread that should lock the group's
        // existence until it exits.
        GroupKeeper(CUDTUnited& glob, CUDTSocket* s) { group = glob.acquireSocketsGroup(s); }

        ~GroupKeeper()
        {
            if (group)
            {
                // We have a guarantee that if `group` was set
                // as non-NULL here, it is also acquired and will not
                // be deleted until this busy flag is set back to false.
                sync::ScopedLock cgroup(*group->exp_groupLock());
                group->apiRelease();
                // Only now that the group lock is lifted, can the
                // group be now deleted and this pointer potentially dangling
            }
        }
    };

#endif
    void updateMux(CUDTSocket* s, const sockaddr_any& addr, const UDPSOCKET* = NULL);
    bool updateListenerMux(CUDTSocket* s, const CUDTSocket* ls);

    // Utility functions for updateMux
    void     configureMuxer(CMultiplexer& w_m, const CUDTSocket* s, int af);
    uint16_t installMuxer(CUDTSocket* w_s, CMultiplexer& sm);

    /// @brief Checks if channel configuration matches the socket configuration.
    /// @param cfgMuxer multiplexer configuration.
    /// @param cfgSocket socket configuration.
    /// @return tru if configurations match, false otherwise.
    static bool channelSettingsMatch(const CSrtMuxerConfig& cfgMuxer, const CSrtConfig& cfgSocket);
    static bool inet6SettingsCompat(const sockaddr_any& muxaddr, const CSrtMuxerConfig& cfgMuxer,
        const sockaddr_any& reqaddr, const CSrtMuxerConfig& cfgSocket);

private:
    // UDP复用器map
    std::map<int, CMultiplexer> m_mMultiplexer; // UDP multiplexer
    // UDP复用器锁
    sync::Mutex                 m_MultiplexerLock;

private:
    CCache<CInfoBlock>* m_pCache; // UDT network information cache

private:
    srt::sync::atomic<bool> m_bClosing;
    sync::Mutex             m_GCStopLock;
    sync::Condition         m_GCStopCond;

    sync::Mutex m_InitLock;
    int         m_iInstanceCount; // number of startup() called by application
    bool        m_bGCStatus;      // if the GC thread is working (true)

    // 资源回收线程
    sync::CThread m_GCThread;

    static void*  garbageCollect(void*);

    sockets_t m_ClosedSockets; // temporarily store closed sockets
#if ENABLE_BONDING
    groups_t m_ClosedGroups;
#endif

    void checkBrokenSockets();
    void removeSocket(const SRTSOCKET u);

    CEPoll m_EPoll; // handling epoll data structures and events

private:
    CUDTUnited(const CUDTUnited&);
    CUDTUnited& operator=(const CUDTUnited&);
};

} // namespace srt

#endif
