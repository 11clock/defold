#include "socket.h"

#include <fcntl.h>

#if defined(__linux__) || defined(__MACH__)
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include "log.h"

namespace dmSocket
{
#if defined(__linux__) || defined(__MACH__)
    #define DM_SOCKET_ERRNO errno
    #define DM_SOCKET_HERRNO h_errno
#else
    #define DM_SOCKET_ERRNO WSAGetLastError()
    #define DM_SOCKET_HERRNO WSAGetLastError()
#endif

#if defined(_WIN32)
    #define DM_SOCKET_NATIVE_TO_RESULT_CASE(x) case WSAE##x: return RESULT_##x
#else
    #define DM_SOCKET_NATIVE_TO_RESULT_CASE(x) case E##x: return RESULT_##x
#endif
    static Result NativeToResult(int r)
    {
        switch (r)
        {
            DM_SOCKET_NATIVE_TO_RESULT_CASE(ACCES);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(AFNOSUPPORT);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(WOULDBLOCK);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(BADF);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(CONNRESET);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(DESTADDRREQ);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(FAULT);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(HOSTUNREACH);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(INTR);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(INVAL);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(ISCONN);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(MFILE);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(MSGSIZE);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(NETDOWN);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(NETUNREACH);
            //DM_SOCKET_NATIVE_TO_RESULT_CASE(NFILE);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(NOBUFS);
            //DM_SOCKET_NATIVE_TO_RESULT_CASE(NOENT);
            //DM_SOCKET_NATIVE_TO_RESULT_CASE(NOMEM);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(NOTCONN);
            //DM_SOCKET_NATIVE_TO_RESULT_CASE(NOTDIR);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(NOTSOCK);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(OPNOTSUPP);
            //DM_SOCKET_NATIVE_TO_RESULT_CASE(PIPE);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(PROTONOSUPPORT);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(PROTOTYPE);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(TIMEDOUT);

            DM_SOCKET_NATIVE_TO_RESULT_CASE(ADDRNOTAVAIL);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(CONNREFUSED);
            DM_SOCKET_NATIVE_TO_RESULT_CASE(ADDRINUSE);
        }

        // TODO: Add log-domain support
        dmLogError("SOCKET: Unknown result code %d\n", r);
        return RESULT_UNKNOWN;
    }
    #undef DM_SOCKET_NATIVE_TO_RESULT_CASE

    #define DM_SOCKET_HNATIVE_TO_RESULT_CASE(x) case x: return RESULT_##x
    static Result HNativeToResult(int r)
    {
        switch (r)
        {
            DM_SOCKET_HNATIVE_TO_RESULT_CASE(HOST_NOT_FOUND);
            DM_SOCKET_HNATIVE_TO_RESULT_CASE(TRY_AGAIN);
            DM_SOCKET_HNATIVE_TO_RESULT_CASE(NO_RECOVERY);
            DM_SOCKET_HNATIVE_TO_RESULT_CASE(NO_DATA);
        }

        // TODO: Add log-domain support
        dmLogError("SOCKET: Unknown result code %d\n", r);
        return RESULT_UNKNOWN;
    }
    #undef DM_SOCKET_HNATIVE_TO_RESULT_CASE

    Result Setup()
    {
#ifdef _WIN32
        WORD version_requested = MAKEWORD(2, 2);
        WSADATA wsa_data;
        int err = WSAStartup(version_requested, &wsa_data);
        if (err != 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;        
        }
#else
        return RESULT_OK;        
#endif
    }

    Result Shutdown()
    {
#ifdef _WIN32
        WSACleanup();
#endif
        return RESULT_OK;
    }

    Result New(Type type, Protocol protocol, Socket* socket)
    {
        int s = ::socket(PF_INET, type, protocol);
        if (s < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            *socket = s;
            return RESULT_OK;
        }
    }

    Result SetReuseAddress(Socket socket, bool reuse)
    {
        int on = (int) reuse;
        int ret;
#ifdef _WIN32
        ret = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*) &on, sizeof(on));
#else
        ret = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Delete(Socket socket)
    {
#if defined(__linux__) || defined(__MACH__)
        int ret = close(socket);
#else
        int ret = closesocket(socket);
#endif
        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Accept(Socket socket, Address* address, Socket* accept_socket)
    {
        struct sockaddr_in sock_addr;
#ifdef _WIN32
        int sock_addr_len = sizeof(sock_addr);
#else
        socklen_t sock_addr_len = sizeof(sock_addr);
#endif
        int s = accept(socket, (struct sockaddr *) &sock_addr, &sock_addr_len);

        if (s < 0)
        {
            *address = sock_addr.sin_addr.s_addr;
            *accept_socket = s;
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            *accept_socket = s;
            return RESULT_OK;
        }
    }

    Result Bind(Socket socket, Address address, int port)
    {
        struct sockaddr_in sock_addr;
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_addr.s_addr = address;
        sock_addr.sin_port  = htons(port);
        int ret = bind(socket, (struct sockaddr *) &sock_addr, sizeof(sock_addr));

        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Connect(Socket socket, Address address, int port)
    {
        struct sockaddr_in sock_addr;
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_addr.s_addr = address;
        sock_addr.sin_port  = htons(port);
        int ret =connect(socket, (struct sockaddr *) &sock_addr, sizeof(sock_addr));

        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Listen(Socket socket, int backlog)
    {
        int ret = listen(socket, backlog);
        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Shutdown(Socket socket, ShutdownType how)
    {
        int ret = shutdown(socket, how);
        if (ret < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
    }

    Result Send(Socket socket, const void* buffer, int length, int* sent_bytes)
    {
        *sent_bytes = 0;
#ifdef _WIN32
        int s = send(socket, (const char*) buffer, length, 0);
#else
        ssize_t s = send(socket, buffer, length, 0);
#endif
        if (s < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            *sent_bytes = s;
            return RESULT_OK;
        }
    }

    Result Receive(Socket socket, void* buffer, int length, int* received_bytes)
    {
        *received_bytes = 0;
#ifdef _WIN32
        int r = recv(socket, (char*) buffer, length, 0);
#else
        int r = recv(socket, buffer, length, 0);
#endif

        if (r < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            *received_bytes = r;
            return RESULT_OK;
        }
    }

    Result SetBlocking(Socket socket, bool blocking)
    {
#if defined(__linux__) || defined(__MACH__)
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }

        if (blocking)
        {
            flags &= ~O_NONBLOCK;
        }
        else
        {
            flags |= O_NONBLOCK;
        }

        if (fcntl(socket, F_SETFL, flags) < 0)
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }
        else
        {
            return RESULT_OK;
        }
#else
        u_long arg;
        if (blocking)
        {
            arg = 0;
        }
        else
        {
            arg = 1;
        }

        int ret = ioctlsocket(socket, FIONBIO, &arg);
        if (ret == 0)
        {
            return RESULT_OK;
        }
        else
        {
            return NativeToResult(DM_SOCKET_ERRNO);
        }

#endif
    }

    Address AddressFromIPString(const char* address)
    {
        return inet_addr(address);
    }

    Result GetHostByName(const char* name, Address* address)
    {
        struct hostent* host = gethostbyname(name);
        if (host)
        {
            *address = *((unsigned long *) host->h_addr_list[0]);
            return RESULT_OK;
        }
        else
        {
            return HNativeToResult(DM_SOCKET_HERRNO);
        }
    }

    #define DM_SOCKET_RESULT_TO_STRING_CASE(x) case RESULT_##x: return #x;
    const char* ResultToString(Result r)
    {
        switch (r)
        {
            DM_SOCKET_RESULT_TO_STRING_CASE(OK);

            DM_SOCKET_RESULT_TO_STRING_CASE(ACCES);
            DM_SOCKET_RESULT_TO_STRING_CASE(AFNOSUPPORT);
            DM_SOCKET_RESULT_TO_STRING_CASE(WOULDBLOCK);
            DM_SOCKET_RESULT_TO_STRING_CASE(BADF);
            DM_SOCKET_RESULT_TO_STRING_CASE(CONNRESET);
            DM_SOCKET_RESULT_TO_STRING_CASE(DESTADDRREQ);
            DM_SOCKET_RESULT_TO_STRING_CASE(FAULT);
            DM_SOCKET_RESULT_TO_STRING_CASE(HOSTUNREACH);
            DM_SOCKET_RESULT_TO_STRING_CASE(INTR);
            DM_SOCKET_RESULT_TO_STRING_CASE(INVAL);
            DM_SOCKET_RESULT_TO_STRING_CASE(ISCONN);
            DM_SOCKET_RESULT_TO_STRING_CASE(MFILE);
            DM_SOCKET_RESULT_TO_STRING_CASE(MSGSIZE);
            DM_SOCKET_RESULT_TO_STRING_CASE(NETDOWN);
            DM_SOCKET_RESULT_TO_STRING_CASE(NETUNREACH);
            //DM_SOCKET_RESULT_TO_STRING_CASE(NFILE);
            DM_SOCKET_RESULT_TO_STRING_CASE(NOBUFS);
            //DM_SOCKET_RESULT_TO_STRING_CASE(NOENT);
            //DM_SOCKET_RESULT_TO_STRING_CASE(NOMEM);
            DM_SOCKET_RESULT_TO_STRING_CASE(NOTCONN);
            //DM_SOCKET_RESULT_TO_STRING_CASE(NOTDIR);
            DM_SOCKET_RESULT_TO_STRING_CASE(NOTSOCK);
            DM_SOCKET_RESULT_TO_STRING_CASE(OPNOTSUPP);
            //DM_SOCKET_RESULT_TO_STRING_CASE(PIPE);
            DM_SOCKET_RESULT_TO_STRING_CASE(PROTONOSUPPORT);
            DM_SOCKET_RESULT_TO_STRING_CASE(PROTOTYPE);
            DM_SOCKET_RESULT_TO_STRING_CASE(TIMEDOUT);

            DM_SOCKET_RESULT_TO_STRING_CASE(ADDRNOTAVAIL);
            DM_SOCKET_RESULT_TO_STRING_CASE(CONNREFUSED);
            DM_SOCKET_RESULT_TO_STRING_CASE(ADDRINUSE);

            DM_SOCKET_RESULT_TO_STRING_CASE(HOST_NOT_FOUND);
            DM_SOCKET_RESULT_TO_STRING_CASE(TRY_AGAIN);
            DM_SOCKET_RESULT_TO_STRING_CASE(NO_RECOVERY);
            DM_SOCKET_RESULT_TO_STRING_CASE(NO_DATA);

            DM_SOCKET_RESULT_TO_STRING_CASE(UNKNOWN);
        }
        // TODO: Add log-domain support
        dmLogError("Unable to convert result %d to string", r);

        return "RESULT_UNDEFINED";
    }
    #undef DM_SOCKET_RESULT_TO_STRING_CASE
}
