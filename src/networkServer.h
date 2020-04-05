#ifndef networkServer_h
#define networkServer_h

#include <netinet/in.h>

#include "ecatDriver.h"

// Configuration    ************************************************************************
#ifndef NUMIPSERVERS // Allow setting from CMake
#define NUMIPSERVERS 50
#endif

#ifndef TCPPORT      // Allow setting from CMake
#define TCPPORT 4200
#endif

#define IPSERVER_PAUSE 10000

#define BUFFLEN 1024 // String buffer length

// Data types       ************************************************************************
struct IPserverThreads {
    struct sockaddr_in client;
    int connfd;

    pthread_t thread;
    int  ipServerNum;
    char inUse;
};

// Functions        ************************************************************************
int writeMapping(char* buff_out, struct mappings_PDO* mapping, int connfd);
void chatThread( void* ptr );
void mainIPserver( void* ptr );

#endif