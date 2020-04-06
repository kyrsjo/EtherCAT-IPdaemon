#include "networkServer.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <pthread.h>

#include "ethercat.h" // Command 'dump' is directly accessing the IOmap

#include "EtherCatDaemon.h"
#include "ecatDriver.h"

//Socket on the server
struct sockaddr_in servaddr;
int sockfd;
// Array of server connection slots
struct IPserverThreads IPservers[NUMIPSERVERS];

int writeMapping(char* buff_out, struct mappings_PDO* mapping, int connfd) {
    //Helper function for chatThread()

    char hstr[BUFFLEN]; // String buffer for conversion functions
    memset(hstr,0,BUFFLEN);

    int numChars = snprintf(buff_out, BUFFLEN,
                            "  [0x%4.4X.%1d] %d:0x%4.4X:0x%2.2X 0x%2.2X %-12s %s\n",
                            mapping->offset, mapping->bitoff,
                            mapping->slaveIdx, mapping->idx, mapping->subidx,
                            mapping->bitlen, dtype2string(mapping->dataType, hstr, BUFFLEN), mapping->name);
    if (numChars < 0 || numChars >= BUFFLEN) {
        memset(buff_out,0,BUFFLEN);
        snprintf(buff_out, BUFFLEN,
                 "err: Internal in writeMapping; numChars = %d\n", numChars);
        return 1;
    }
    write(connfd, buff_out, numChars);
    memset(buff_out, 0, numChars); //Don't need to zero everything every time
}
void chatThread(void* ptr) {
    //Runs in it's own thread, talking to one client
    // NOTE: To test with TELNET client, LINEMODE must be used!
    struct IPserverThreads* myThread = (struct IPserverThreads*) ptr;

    char buff_in[BUFFLEN];
    char buff_out[BUFFLEN];
    memset(buff_in,       0, BUFFLEN);
    memset(buff_out,      0, BUFFLEN);

    char hstr[BUFFLEN]; // String buffer for conversion functions
    memset(hstr,0,BUFFLEN);

    boolean didRepeat     = FALSE;
    char    buff_in_prev[BUFFLEN];
    memset(buff_in_prev,  0, BUFFLEN);

    while (1) {
        //Check that we didn't get a SIGPIPE
        if (myThread->gotSIGPIPE == 1) {
            pthread_mutex_lock(&printf_lock);
            printf("slot %d got SIGPIPE (client %s) \n", myThread->ipServerNum, inet_ntoa(myThread->client.sin_addr));
            pthread_mutex_unlock(&printf_lock);
            goto endcom;
        }

        //Tell the client that we are ready for the next command
        strncpy(buff_out,"ok\n",BUFFLEN);
        write(myThread->connfd, buff_out, BUFFLEN);
        memset(buff_out,0,BUFFLEN);

        int numBytes = read(myThread->connfd, buff_in, BUFFLEN);
        if (numBytes >= BUFFLEN || buff_in[BUFFLEN-1] != '\0') {
            //Note: Last byte in buff should always be \0.
            pthread_mutex_lock(&printf_lock);
            fprintf(stderr, "ERROR, message too long");
            pthread_mutex_unlock(&printf_lock);
            goto endcom;
        }
        //printf("GOT: (%d) '%s'\n", strlen(buff_in), buff_in);

        //Replay last command
        if (buff_in[0] =='\n' || buff_in[0] == '\r')  {  // linebreak -> replay previous cmd
            if (buff_in_prev[0] == '\0') {
                strncpy(buff_out, "err: No previous command available.\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
                goto donecmds;
            }
            else {
                memset(buff_in,0,BUFFLEN);
                memcpy(buff_in, buff_in_prev, BUFFLEN);
                didRepeat = TRUE;
            }
        }

        // Command parsing
        if      (!strncmp(buff_in, "bye",      3))  {  // bye
            //Terminate this connection
            goto endcom;
        }
        else if (!strncmp(buff_in, "quit",     4))  {  // quit
            pthread_mutex_lock(&printf_lock);
            printf("quit from slot %d address %s \n", myThread->ipServerNum, inet_ntoa(myThread->client.sin_addr));
            pthread_mutex_unlock(&printf_lock);
            close(myThread->connfd);
            close(sockfd);
            gotCtrlC=1;

            goto endcom;
        }
        else if (!strncmp(buff_in, "help",    4))  {  // help
            int buffUsed = 0;
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  ACCEPTED COMMANDS:\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'bye'                     End this network connection\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'quit'                    Virtual Control+C on the server\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'dump'                    Dump the current IOmap\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'meta all'                Show mappings for all PDOs\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'meta slave:idx:subidx'   Show mappings for given PDO (format int:hex:hex)\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  'get slave:idx:subidx'    Get current value for given PDO (format int:hex:hex)\n");
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                 "  '\\r' or '\\n' (ENTER)      Repeat previous command\n");
            if (buffUsed >= BUFFLEN) {
                pthread_mutex_lock(&printf_lock);
                fprintf(stderr, "ERROR: buff_out overextended\n");
                pthread_mutex_unlock(&printf_lock);
                exit(1);
            }

            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out, 0, BUFFLEN);
        }
        else if (!strncmp(buff_in, "dump",    4))  {  // dump
            //Dump the current raw IOmap content
            if (!inOP || !updating) {
                strncpy(buff_out, "err: not inOP or not updating\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out, 0, BUFFLEN);
                goto donecmds;
            }

            pthread_mutex_lock(&IOmap_lock);

            snprintf(buff_out, BUFFLEN, "  T:%" PRId64 ";\n",ec_DCtime);
            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out, 0, BUFFLEN);

            for (uint16 slave = 1; slave <= ec_slavecount; slave++) {
                int buffUsed = 0;

                buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed, "  slave[%d]:", slave);
                buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed, " O:");

                int nChars = ec_slave[slave].Obytes;
                if (nChars==0 && ec_slave[slave].Obits > 0) nChars = 1;
                for(int j = 0 ; j < nChars ; j++) {
                    buffUsed +=snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                        " %2.2x", *(ec_slave[slave].outputs + j));
                }

                buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed, " I:");

                nChars = ec_slave[slave].Ibytes;
                if (nChars==0 && ec_slave[slave].Ibits > 0) nChars = 1;
                for(int j = 0 ; j < nChars ; j++) {
                    buffUsed +=snprintf(buff_out+buffUsed, BUFFLEN-buffUsed,
                                        " %2.2x", *(ec_slave[slave].inputs + j));
                }

                buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed, "\n", slave);

                if (buffUsed >= BUFFLEN) {
                    pthread_mutex_lock(&printf_lock);
                    printf("ERROR: buff_out overextended\n");
                    pthread_mutex_unlock(&printf_lock);
                    exit(1);
                }
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out, 0, BUFFLEN);
            }
            pthread_mutex_unlock(&IOmap_lock);

        }
        else if (!strncmp(buff_in, "meta all", 8))  {  // meta all
            //Metadata about all slaves/indexes/subindexes
            struct mappings_PDO* mapping_active;

            strncpy(buff_out, "  OUTPUTS:\n", BUFFLEN);
            write(myThread->connfd, buff_out, BUFFLEN);

            mapping_active = mapping_out;
            while (mapping_active->bitlen > 0) {
                writeMapping(buff_out, mapping_active, myThread->connfd);
                mapping_active = mapping_active->next;
            }

            strncpy(buff_out, "  INPUTS:\n", BUFFLEN);
            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out,0,BUFFLEN);

            mapping_active = mapping_in;
            while (mapping_active->bitlen > 0) {
                writeMapping(buff_out, mapping_active, myThread->connfd);
                mapping_active = mapping_active->next;
            }
        }
        else if (!strncmp(buff_in, "meta ",    5))  {  // meta slave:idx:subidx
            //Metadata about a given PDO
            uint16 slave  = 0;
            uint16 idx    = 0;
            uint8  subidx = 0;
            if (sscanf(buff_in,"meta %hi:%hx:%hhx", &slave, &idx, &subidx) == 3){
                //printf("%d:%x:%x\n", slave,idx,subidx);
                strncpy(buff_out, "err: meta slave:idx:subidx NOT YET IMPLEMENTED\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
            }
            else {
                strncpy(buff_out, "err: meta got bad args\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
            }

        }
        else if (!strncmp(buff_in, "get ",     4))  {  // get slave:idx:subidx
            //Data from a given PDO
            uint16 slave  = 0;
            uint16 idx    = 0;
            uint8  subidx = 0;
            if (sscanf(buff_in,"get %hi:%hx:%hhx", &slave, &idx, &subidx) != 3){
                strncpy(buff_out, "err: get got bad args\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
                goto donecmds;
            }

            //printf("%d:%x:%x\n", slave,idx,subidx);

            struct mappings_PDO* dataMapping = get_address(slave, idx, subidx, mapping_in);
            if (dataMapping == NULL) {
                snprintf(buff_out, BUFFLEN, "err: PDO address %d:%x:%x not recognized (searched for inputs)\n", slave,idx,subidx);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
                goto donecmds;
            }

            if (!inOP || !updating) {
                strncpy(buff_out, "err: not inOP or not updating\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out, 0, BUFFLEN);
                goto donecmds;
            }

            int buffUsed = 0;
            pthread_mutex_lock(&IOmap_lock);
            PDOval2string(dataMapping, hstr, BUFFLEN);
            pthread_mutex_unlock(&IOmap_lock);
            buffUsed += snprintf(buff_out, BUFFLEN, "  %s", hstr);
            memset(hstr,0,BUFFLEN);

            dtype2string(dataMapping->dataType, hstr, BUFFLEN);
            buffUsed += snprintf(buff_out+buffUsed, BUFFLEN-buffUsed, "  %s\n", hstr);
            memset(hstr,0,BUFFLEN);

            if (buffUsed >= BUFFLEN) {
                pthread_mutex_lock(&printf_lock);
                printf("ERROR: buff_out overextended\n");
                pthread_mutex_unlock(&printf_lock);
                exit(1);
            }

            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out,0,BUFFLEN);

        }
        else {                                      // (unknown command)
            snprintf(buff_out, BUFFLEN, "err: unknown command '%s'\n",buff_in);
            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out,0,BUFFLEN);
        }

    donecmds: // Escape from inside an input handler

        //Reset the buffer before the next round
        if (didRepeat) {
            //This was a repeat; just reset the flag.
            didRepeat = FALSE;
        }
        else {
            //Not a repeat; copy this command to last.
            memset(buff_in_prev,0,BUFFLEN);
            memcpy(buff_in_prev,buff_in,BUFFLEN);
        }
        memset(buff_in,0,BUFFLEN);
    }

endcom: // Escape from the loop

    pthread_mutex_lock(&printf_lock);
    printf("Finished: -- slot %d disconnecting from %s \n", myThread->ipServerNum, inet_ntoa(myThread->client.sin_addr));
    pthread_mutex_unlock(&printf_lock);

    memset(buff_out, 0, BUFFLEN);
    strncpy(buff_out, "bye\n", BUFFLEN);
    write(myThread->connfd, buff_out, 4);

    close(myThread->connfd);
    myThread->inUse = 0;

    pthread_exit(0);
}

void mainIPserver( void* ptr ) {
    // IP connection handling and server spinup
    // Inspired by https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/

    (void)ptr; // Not used, reference it to quiet down the compiler

    //Setup SIGPIPE handler
    signal(SIGPIPE, &SIGPIPE_handler);

    //Initialize IPservers array
    memset(IPservers, 0, sizeof(struct IPserverThreads)*NUMIPSERVERS);

    //Wait for root privs to be dropped
    pthread_mutex_lock(&rootprivs_lock);

    //Create the server socket...
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("ERROR when opening socket");
        exit(1);
    }

    int enableReuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enableReuse, sizeof(int)) < 0){
        pthread_mutex_lock(&printf_lock);
        fprintf(stderr, "WARNING: setsockopt(SO_REUSEADDR) failed");
        pthread_mutex_unlock(&printf_lock);
    }

    //Set server IP and port
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCPPORT);

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0) {
        perror("ERROR: Socket bind failed");
        pthread_mutex_lock(&printf_lock);
        fprintf(stderr, "if 'netstat | grep %d' shows TIME_WAIT, please wait for the OS timeout to finish\n", TCPPORT);
        pthread_mutex_unlock(&printf_lock);
        close(sockfd);
        exit(1);
    }

    //Listen to the socket...
    if ((listen(sockfd, 5)) != 0) {
        pthread_mutex_lock(&printf_lock);
        perror("ERROR: Socket listen failed");
        pthread_mutex_unlock(&printf_lock);
        exit(1);
    }
    pthread_mutex_lock(&printf_lock);
    printf("listen OK\n");
    pthread_mutex_unlock(&printf_lock);


    while(1) {
        //Find a free IPservers listing
        int ipServerNum = 0;
        for (ipServerNum = 0; ipServerNum < NUMIPSERVERS; ipServerNum++){
            if (IPservers[ipServerNum].inUse == 0) { // Found a free one; let's zero it and break
                memset(&(IPservers[ipServerNum]), 0, sizeof(struct IPserverThreads));
                IPservers[ipServerNum].ipServerNum = ipServerNum;
                break;
            }
        }
        if (ipServerNum == NUMIPSERVERS) {
            pthread_mutex_lock(&printf_lock);
            printf("Too many clients!\n");
            pthread_mutex_unlock(&printf_lock);
            goto noSock;
        }

        IPservers[ipServerNum].inUse = 1;

        int addrlen = sizeof(IPservers[ipServerNum].client);
        IPservers[ipServerNum].connfd = accept(sockfd, (struct sockaddr*)&(IPservers[ipServerNum].client), &addrlen);

        if (IPservers[ipServerNum].connfd < 0) {
            perror("ERROR: Server accept connection failed");
            IPservers[ipServerNum].inUse == 0;
            goto noSock;
        }
        pthread_mutex_lock(&printf_lock);
        printf("IP server slot %d connected to host %s \n", ipServerNum, inet_ntoa(IPservers[ipServerNum].client.sin_addr));
        pthread_mutex_unlock(&printf_lock);

        //Here using Linux pthreads, not OSAL,
        // because we want to do more than just creating the threads.
        // When done, these threads close their connection and set inUse = 0.
        pthread_create(&(IPservers[ipServerNum].thread), NULL, (void*) &chatThread, (void*) &(IPservers[ipServerNum]));

    noSock:
        // Probably not needed, may even be harmfull to performance?
        // Should probably be moved to "if (ipServerNum == NUMIPSERVERS)"
        osal_usleep(IPSERVER_PAUSE);
    }
}

void SIGPIPE_handler (int signal) {
    //Interupt handler for SIGPIPE
    //Handle it equivalent to command 'bye'

    pthread_t thisThread = pthread_self();

    int i;
    for (i = 0; i < NUMIPSERVERS; i++) {
        if (IPservers[i].thread == thisThread) break;
    }
    if (i == NUMIPSERVERS) {
        abort(); // The thread wasn't found.
    }
    IPservers[i].gotSIGPIPE = 1;

}