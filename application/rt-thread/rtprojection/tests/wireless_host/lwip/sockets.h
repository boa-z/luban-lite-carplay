/* SPDX-License-Identifier: GPL-3.0-only */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#define lwip_socket socket
#define lwip_ioctl ioctl
#define lwip_bind bind
#define lwip_listen listen
#define lwip_accept accept
#define lwip_setsockopt setsockopt
#define lwip_select select
#define lwip_recv recv
#define lwip_send send
#define lwip_close close
#define lwip_shutdown shutdown
