/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

#include <windows.h>
#include <winsock2.h>
#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"

#include "nio.h"
#include "nio_util.h"
#include "net_util.h"

#include "sun_nio_ch_Net.h"

/**
 * Definitions to allow for building with older SDK include files.
 */

#ifndef MCAST_BLOCK_SOURCE

#define MCAST_BLOCK_SOURCE          43
#define MCAST_UNBLOCK_SOURCE        44
#define MCAST_JOIN_SOURCE_GROUP     45
#define MCAST_LEAVE_SOURCE_GROUP    46

#endif  /* MCAST_BLOCK_SOURCE */

typedef struct my_ip_mreq_source {
    IN_ADDR imr_multiaddr;
    IN_ADDR imr_sourceaddr;
    IN_ADDR imr_interface;
};

typedef struct my_group_source_req {
    ULONG gsr_interface;
    SOCKADDR_STORAGE gsr_group;
    SOCKADDR_STORAGE gsr_source;
};

/**
 * Copy IPv6 address as jbytearray to target
 */
#define COPY_INET6_ADDRESS(env, source, target) \
    (*env)->GetByteArrayRegion(env, source, 0, 16, target)



JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_initIDs(JNIEnv *env, jclass clazz)
{
    /* nothing to do */
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_Net_isIPv6Available0(JNIEnv* env, jclass cl)
{
    /*
     * Return true if Windows Vista or newer, and IPv6 is configured
     */
    OSVERSIONINFO ver;
    ver.dwOSVersionInfoSize = sizeof(ver);
    GetVersionEx(&ver);
    if ((ver.dwPlatformId == VER_PLATFORM_WIN32_NT) &&
        (ver.dwMajorVersion >= 6)  && ipv6_available())
    {
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_socket0(JNIEnv *env, jclass cl, jboolean preferIPv6,
                            jboolean stream, jboolean reuse)
{
    SOCKET s;
    int domain = (preferIPv6) ? AF_INET6 : AF_INET;

    s = socket(domain, (stream ? SOCK_STREAM : SOCK_DGRAM), 0);
    if (s != INVALID_SOCKET) {
        SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);

        /* IPV6_V6ONLY is true by default */
        if (domain == AF_INET6) {
            int opt = 0;
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char *)&opt, sizeof(opt));
        }
    } else {
        NET_ThrowNew(env, WSAGetLastError(), "socket");
    }

    return (jint)s;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_bind0(JNIEnv *env, jclass clazz, jboolean preferIPv6,
                          jobject fdo, jobject iao, jint port)
{
    SOCKETADDRESS sa;
    int rv;
    int sa_len;

    if (NET_InetAddressToSockaddr(env, iao, port, (struct sockaddr *)&sa, &sa_len, preferIPv6) != 0) {
      return;
    }

    rv = NET_Bind(fdval(env, fdo), (struct sockaddr *)&sa, sa_len);
    if (rv == SOCKET_ERROR)
        NET_ThrowNew(env, WSAGetLastError(), "bind");
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_listen(JNIEnv *env, jclass cl, jobject fdo, jint backlog)
{
    if (listen(fdval(env,fdo), backlog) == SOCKET_ERROR) {
        NET_ThrowNew(env, WSAGetLastError(), "listen");
    }
}


JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_connect0(JNIEnv *env, jclass clazz, jboolean preferIPv6, jobject fdo,
                             jobject iao, jint port)
{
    SOCKETADDRESS sa;
    int rv;
    int sa_len;

    if (NET_InetAddressToSockaddr(env, iao, port, (struct sockaddr *)&sa, &sa_len, preferIPv6) != 0) {
        return IOS_THROWN;
    }

    rv = connect(fdval(env, fdo), (struct sockaddr *)&sa, sa_len);
    if (rv != 0) {
        int err = WSAGetLastError();
        if (err == WSAEINPROGRESS || err == WSAEWOULDBLOCK) {
            return IOS_UNAVAILABLE;
        }
        NET_ThrowNew(env, err, "connect");
        return IOS_THROWN;
    }
    return 1;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_localPort(JNIEnv *env, jclass clazz, jobject fdo)
{
    SOCKETADDRESS sa;
    int sa_len = sizeof(sa);

    if (getsockname(fdval(env, fdo), (struct sockaddr *)&sa, &sa_len) < 0) {
        int error = WSAGetLastError();
        if (error == WSAEINVAL) {
            return 0;
        }
        NET_ThrowNew(env, error, "getsockname");
        return IOS_THROWN;
    }
    return NET_GetPortFromSockaddr((struct sockaddr *)&sa);
}

JNIEXPORT jobject JNICALL
Java_sun_nio_ch_Net_localInetAddress(JNIEnv *env, jclass clazz, jobject fdo)
{
    SOCKETADDRESS sa;
    int sa_len = sizeof(sa);
    int port;

    if (getsockname(fdval(env, fdo), (struct sockaddr *)&sa, &sa_len) < 0) {
        NET_ThrowNew(env, WSAGetLastError(), "getsockname");
        return NULL;
    }
    return NET_SockaddrToInetAddress(env, (struct sockaddr *)&sa, &port);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_remotePort(JNIEnv *env, jclass clazz, jobject fdo)
{
    SOCKETADDRESS sa;
    int sa_len = sizeof(sa);

    if (getpeername(fdval(env, fdo), (struct sockaddr *)&sa, &sa_len) < 0) {
        int error = WSAGetLastError();
        if (error == WSAEINVAL) {
            return 0;
        }
        NET_ThrowNew(env, error, "getsockname");
        return IOS_THROWN;
    }
    return NET_GetPortFromSockaddr((struct sockaddr *)&sa);
}

JNIEXPORT jobject JNICALL
Java_sun_nio_ch_Net_remoteInetAddress(JNIEnv *env, jclass clazz, jobject fdo)
{
    SOCKETADDRESS sa;
    int sa_len = sizeof(sa);
    int port;

    if (getpeername(fdval(env, fdo), (struct sockaddr *)&sa, &sa_len) < 0) {
        NET_ThrowNew(env, WSAGetLastError(), "getsockname");
        return NULL;
    }
    return NET_SockaddrToInetAddress(env, (struct sockaddr *)&sa, &port);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_getIntOption0(JNIEnv *env, jclass clazz, jobject fdo,
                                  jboolean mayNeedConversion, jint level, jint opt)
{
    int result = 0;
    struct linger linger;
    char *arg;
    int arglen, n;

    if (level == SOL_SOCKET && opt == SO_LINGER) {
        arg = (char *)&linger;
        arglen = sizeof(linger);
    } else {
        arg = (char *)&result;
        arglen = sizeof(result);
    }

    /**
     * HACK: IP_TOS is deprecated on Windows and querying the option
     * returns a protocol error. NET_GetSockOpt handles this and uses
     * a fallback mechanism.
     */
    if (level == IPPROTO_IP && opt == IP_TOS) {
        mayNeedConversion = JNI_TRUE;
    }

    if (mayNeedConversion) {
        n = NET_GetSockOpt(fdval(env, fdo), level, opt, arg, &arglen);
    } else {
        n = getsockopt(fdval(env, fdo), level, opt, arg, &arglen);
    }
    if (n < 0) {
        handleSocketError(env, WSAGetLastError());
        return IOS_THROWN;
    }

    if (level == SOL_SOCKET && opt == SO_LINGER)
        return linger.l_onoff ? linger.l_linger : -1;
    else
        return result;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_setIntOption0(JNIEnv *env, jclass clazz, jobject fdo,
                                  jboolean mayNeedConversion, jint level, jint opt, jint arg)
{
    struct linger linger;
    char *parg;
    int arglen, n;

    if (level == SOL_SOCKET && opt == SO_LINGER) {
        parg = (char *)&linger;
        arglen = sizeof(linger);
        if (arg >= 0) {
            linger.l_onoff = 1;
            linger.l_linger = (unsigned short)arg;
        } else {
            linger.l_onoff = 0;
            linger.l_linger = 0;
        }
    } else {
        parg = (char *)&arg;
        arglen = sizeof(arg);
    }

    if (mayNeedConversion) {
        n = NET_SetSockOpt(fdval(env, fdo), level, opt, parg, arglen);
    } else {
        n = setsockopt(fdval(env, fdo), level, opt, parg, arglen);
    }
    if (n < 0)
        handleSocketError(env, WSAGetLastError());
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_joinOrDrop4(JNIEnv *env, jobject this, jboolean join, jobject fdo,
                                jint group, jint interf, jint source)
{
    struct ip_mreq mreq;
    struct my_ip_mreq_source mreq_source;
    int opt, n, optlen;
    void* optval;

    if (source == 0) {
        mreq.imr_multiaddr.s_addr = htonl(group);
        mreq.imr_interface.s_addr = htonl(interf);
        opt = (join) ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
        optval = (void*)&mreq;
        optlen = sizeof(mreq);
    } else {
        mreq_source.imr_multiaddr.s_addr = htonl(group);
        mreq_source.imr_sourceaddr.s_addr = htonl(source);
        mreq_source.imr_interface.s_addr = htonl(interf);
        opt = (join) ? IP_ADD_SOURCE_MEMBERSHIP : IP_DROP_SOURCE_MEMBERSHIP;
        optval = (void*)&mreq_source;
        optlen = sizeof(mreq_source);
    }

    n = setsockopt(fdval(env,fdo), IPPROTO_IP, opt, optval, optlen);
    if (n < 0) {
        if (join && (WSAGetLastError() == WSAENOPROTOOPT))
            return IOS_UNAVAILABLE;
        handleSocketError(env, WSAGetLastError());
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_blockOrUnblock4(JNIEnv *env, jobject this, jboolean block, jobject fdo,
                                   jint group, jint interf, jint source)
{
    struct my_ip_mreq_source mreq_source;
    int n;
    int opt = (block) ? IP_BLOCK_SOURCE : IP_UNBLOCK_SOURCE;

    mreq_source.imr_multiaddr.s_addr = htonl(group);
    mreq_source.imr_sourceaddr.s_addr = htonl(source);
    mreq_source.imr_interface.s_addr = htonl(interf);

    n = setsockopt(fdval(env,fdo), IPPROTO_IP, opt,
                   (void*)&mreq_source, sizeof(mreq_source));
    if (n < 0) {
        if (block && (WSAGetLastError() == WSAENOPROTOOPT))
            return IOS_UNAVAILABLE;
        handleSocketError(env, WSAGetLastError());
    }
    return 0;
}

/**
 * Call setsockopt with a IPPROTO_IPV6 level socket option
 * and a group_source_req structure as the option value. The
 * given IPv6 group, interface index, and IPv6 source address
 * are copied into the structure.
 */
static int setGroupSourceReqOption(JNIEnv* env,
                                   jobject fdo,
                                   int opt,
                                   jbyteArray group,
                                   jint index,
                                   jbyteArray source)
{
    struct my_group_source_req req;
    struct sockaddr_in6* sin6;

    req.gsr_interface = (ULONG)index;

    sin6 = (struct sockaddr_in6*)&(req.gsr_group);
    sin6->sin6_family = AF_INET6;
    COPY_INET6_ADDRESS(env, group, (jbyte*)&(sin6->sin6_addr));

    sin6 = (struct sockaddr_in6*)&(req.gsr_source);
    sin6->sin6_family = AF_INET6;
    COPY_INET6_ADDRESS(env, source, (jbyte*)&(sin6->sin6_addr));

    return setsockopt(fdval(env,fdo), IPPROTO_IPV6, opt, (void*)&req, sizeof(req));
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_joinOrDrop6(JNIEnv *env, jobject this, jboolean join, jobject fdo,
                                jbyteArray group, jint index, jbyteArray source)
{
    struct ipv6_mreq mreq6;
    int n;

    if (source == NULL) {
        int opt = (join) ? IPV6_ADD_MEMBERSHIP : IPV6_DROP_MEMBERSHIP;
        COPY_INET6_ADDRESS(env, group, (jbyte*)&(mreq6.ipv6mr_multiaddr));
        mreq6.ipv6mr_interface = (int)index;
        n = setsockopt(fdval(env,fdo), IPPROTO_IPV6, opt,
                       (void*)&mreq6, sizeof(mreq6));
    } else {
        int opt = (join) ? MCAST_JOIN_SOURCE_GROUP : MCAST_LEAVE_SOURCE_GROUP;
        n = setGroupSourceReqOption(env, fdo, opt, group, index, source);
    }

    if (n < 0) {
        handleSocketError(env, errno);
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_blockOrUnblock6(JNIEnv *env, jobject this, jboolean block, jobject fdo,
                                    jbyteArray group, jint index, jbyteArray source)
{
    int opt = (block) ? MCAST_BLOCK_SOURCE : MCAST_UNBLOCK_SOURCE;
    int n = setGroupSourceReqOption(env, fdo, opt, group, index, source);
    if (n < 0) {
        handleSocketError(env, errno);
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_setInterface4(JNIEnv* env, jobject this, jobject fdo, jint interf)
{
    struct in_addr in;
    int arglen = sizeof(struct in_addr);
    int n;

    in.s_addr = htonl(interf);

    n = setsockopt(fdval(env, fdo), IPPROTO_IP, IP_MULTICAST_IF,
                   (void*)&(in.s_addr), arglen);
    if (n < 0) {
        handleSocketError(env, WSAGetLastError());
    }
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_getInterface4(JNIEnv* env, jobject this, jobject fdo)
{
    struct in_addr in;
    int arglen = sizeof(struct in_addr);
    int n;

    n = getsockopt(fdval(env, fdo), IPPROTO_IP, IP_MULTICAST_IF, (void*)&in, &arglen);
    if (n < 0) {
        handleSocketError(env, WSAGetLastError());
        return IOS_THROWN;
    }
    return ntohl(in.s_addr);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_setInterface6(JNIEnv* env, jobject this, jobject fdo, jint index)
{
    int value = (jint)index;
    int arglen = sizeof(value);
    int n;

    n = setsockopt(fdval(env, fdo), IPPROTO_IPV6, IPV6_MULTICAST_IF,
                   (void*)&(index), arglen);
    if (n < 0) {
        handleSocketError(env, errno);
    }
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_Net_getInterface6(JNIEnv* env, jobject this, jobject fdo)
{
    int index;
    int arglen = sizeof(index);
    int n;

    n = getsockopt(fdval(env, fdo), IPPROTO_IPV6, IPV6_MULTICAST_IF, (void*)&index, &arglen);
    if (n < 0) {
        handleSocketError(env, errno);
        return -1;
    }
    return (jint)index;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_Net_shutdown(JNIEnv *env, jclass cl, jobject fdo, jint jhow) {
    int how = (jhow == sun_nio_ch_Net_SHUT_RD) ? SD_RECEIVE :
        (jhow == sun_nio_ch_Net_SHUT_WR) ? SD_SEND : SD_BOTH;
    if (shutdown(fdval(env, fdo), how) == SOCKET_ERROR) {
        NET_ThrowNew(env, WSAGetLastError(), "shutdown");
    }
}
