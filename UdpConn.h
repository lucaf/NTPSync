//
//  UdpConn.h
//
//  Author: Filippin luca
//  luca.filippin@gmail.com
//

#ifndef __UDPCONN_H__
#define __UDPCONN_H__

int udp_open(char *address, int port, int timeout_ms);
void udp_close(int s);
int udp_send(int s, char *buffer, int len);
int udp_receive(int s, char *buffer, int len);

#endif
