// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)
#ifndef MAC_SOCKET_H
#define MAC_SOCKET_H

#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct MAC_SOCK_Connection MAC_SOCK_Connection;
struct MAC_SOCK_Connection
{
  MAC_SOCK_Connection *next;
  SOCK_Endpoint endpoint;
  SOCK_Protocol protocol;
  int fd;
  B32 listener;
};

typedef struct MAC_SOCK_Header MAC_SOCK_Header;
struct MAC_SOCK_Header
{
  SOCK_Endpoint endpoint;
  U64 size;
  U64 deadline;
  SOCK_Protocol protocol;
};

typedef struct MAC_SOCK_Session MAC_SOCK_Session;
struct MAC_SOCK_Session
{
  Arena *arena;
  GuardedRing *send;
  GuardedRing *recv;
  MAC_SOCK_Connection *connections;
  SOCK_WakeupFunctionType *wakeup;
  U8 *send_buffer;
  Thread thread;
  int queue;
  U32 closing;
};

#endif // MAC_SOCKET_H
