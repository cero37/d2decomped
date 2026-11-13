/* SPDX-License-Identifier: GPL-3.0-or-later */
/* 3DS: no Unix-domain sockets. The names devctl.cpp needs to compile; its
   socket(AF_UNIX) call fails at run time, so --devctl reports an error. */
#pragma once
#include <sys/socket.h>
#ifndef AF_UNIX
#  define AF_UNIX 1
#endif
struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};
