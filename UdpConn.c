//
//  UdpConn.c
//
//  Created by luca on 15/06/2012.
//
//  Simple udp connection manager.
//

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "UdpConn.h"

#define DEBUG_BASIC     0x01
#define DEBUG_MEDIUM    0x02
#define DEBUG_DEEP      0x04

#define DEBUG_SWITCH    DEBUG_BASIC// + DEBUG_MEDIUM + DEBUG_DEEP
#include "DebugUtil.h"

#define UDPCONN_HEADER   "UDP-CONN"
#define UDPCONN_DBG(fmt, ...) eprintf(UDPCONN_HEADER, fmt, __VA_ARGS__)

// On linux SO_NOSIGPIPE doesn't exist
#ifdef __APPLE__

    #define _UDP_SEND_OPT 0

    int _set_no_sigpipe(int s) {
        int flag_no_sigpipe = 1;
        return setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &flag_no_sigpipe, sizeof(flag_no_sigpipe));
    }

#endif

#ifdef __linux__

    #define _UDP_SEND_OPT MSG_NOSIGNAL

    int _set_no_sigpipe(int s) {
        return 0;
    }

#endif

int udp_open(char *address, int port, int timeout_ms) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (s > 0) {
        struct hostent *host = gethostbyname(address);

        if (host != NULL) {
            struct sockaddr_in addr;
            struct timeval tv;
            memset((char *)&addr, 0, sizeof(addr));
            memcpy(&addr.sin_addr, host->h_addr, sizeof(addr.sin_addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            memset(&tv, 0, sizeof(tv));
            tv.tv_usec = timeout_ms;

            if (_set_no_sigpipe(s) == 0 &&
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
                connect(s, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == 0)
                return s;

            close(s);
        }
    }
    return -1;
}

void udp_close(int s) {
    if (s > 0)
        close(s);
}

int udp_send(int s, char *buffer, int len) {
    int n = (int)send(s, buffer, len, _UDP_SEND_OPT);

    if (n < 0)
        DEBUG_LEVEL(DEBUG_BASIC, UDPCONN_DBG("-- Error: failed to send data (%d)\n", errno));

    DEBUG_OPEN(DEBUG_MEDIUM)
    char buff[256];
    hex_dump((unsigned char *)buffer, len, buff, sizeof(buff), 36, eHexDumpMode_hex_and_text, NULL);
    UDPCONN_DBG("-- UDP (out) hex\n%s\n", buff);
    DEBUG_CLOSE

    return n;
}

int udp_receive(int s, char *buffer, int len) {
    int n = (int)recv(s, buffer, len, 0);

    if (n < 0) {
        DEBUG_LEVEL(DEBUG_BASIC, UDPCONN_DBG("-- Error: failed to receive data (%d)\n", errno));
    } else {
        DEBUG_OPEN(DEBUG_MEDIUM)
        char buff[256];
        hex_dump((unsigned char *)buffer, len, buff, sizeof(buff), 36, eHexDumpMode_hex_and_text, NULL);
        UDPCONN_DBG("-- Data (in) hex\n%s\n", buff);
        DEBUG_CLOSE
    }

    return n;
}
