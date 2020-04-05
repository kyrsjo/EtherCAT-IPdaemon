/** \file
 * \brief Example code for Simple Open EtherCAT master
 *
 * Usage : simple_test [ifname1]
 * ifname is NIC interface, f.e. eth0
 *
 * This is a minimal test.
 *
 * (c)Arthur Ketels 2010 - 2011
 */

#include "EtherCatDaemon.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <stdlib.h>
#include <pthread.h>

#include "networkServer.h"
#include "ecatDriver.h"

// Global data      ************************************************************************

// Set to 1 if we got a control+C interupt or if an IP client calls 'quit'
volatile sig_atomic_t gotCtrlC = 0;


// File-global data ************************************************************************

//Persistent threads
OSAL_THREAD_HANDLE thread_PLCwatch;
pthread_t thread_communicate;


// Functions        ************************************************************************

void ctrlC_handler(int signal){
    if(gotCtrlC==1) {
        //User is desperate. Kill it NOW.
        abort();
    }
    else {
        gotCtrlC = 1;
    }
}

int main(int argc, char *argv[]) {
    printf("SOEM (Simple Open EtherCAT Master)\nSimple test\n");

    if (argc == 2) {
        // Read config file
        //TODO

        /* create thread to handle slave error handling in OP */
        //Note: This is technically a library bug;
        // function pointers should not be declared as void*!
        // https://isocpp.org/wiki/faq/pointers-to-members#cant-cvt-fnptr-to-voidptr
        osal_thread_create(&thread_PLCwatch,    128000, (void*) &ecat_check,    (void*) &ctime);

        pthread_create(&thread_communicate, NULL, (void*) &mainIPserver, (void*) &ctime);

        //Interupt handler for Control+c
        signal(SIGINT, ctrlC_handler);

        /* start cyclic part */
        ecat_initialize(argv[1]);
    }
    else {
        printf("Usage:    daemon ifname\n");
        printf("  ifname:   Communication interface, e.g. eth1\n");
    }

    printf("End program\n");
    return (0);
}
