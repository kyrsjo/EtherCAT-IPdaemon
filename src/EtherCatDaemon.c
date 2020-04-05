/*
 * EtherCAT IP daemon
 *
 * This code is reading data from an EtherCAT PLC using the SOEM libary.
 * No control loops etc. are implemented in this code.
 * This code is intended for research purposes only.
 *
 * The code is partially based on the example codes in SOEM, especially simple_example.c and slaveinfo.c.
 *
 * To test it, start the program, then connect to it with a telnet client in line mode on port 4200.
 * The 'help' command will get you started.
 *
 * Licensed under the GNU General Public License version 2 with exceptions.
 * See LICENSE file in the SOEM root for full license information.
 *
 * K. Sjobak, 2020.
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

// Lock for stdin/stdout/stderr (used once we get into threading)
pthread_mutex_t printf_lock;

// File-global data ************************************************************************

//Persistent threads
pthread_t thread_communicate;       // TCP/IP communications

// Functions        ************************************************************************

int main(int argc, char *argv[]) {
    printf("EtherCat daemon, using SOEM\n");
    printf("*** For research purposes ONLY ***\n");

    if (argc == 2) {
        // Read config file
        //TODO

        if (pthread_mutex_init(&printf_lock, NULL) != 0) {
            perror("ERROR pthread_mutex_init has failed for printf_lock");
            exit(1);
        }

        pthread_create(&thread_communicate, NULL, (void*) &mainIPserver, (void*) &ctime);

        //Interupt handler for Control+c
        signal(SIGINT, ctrlC_handler);

        // Start the EtherCAT driver
        ecat_driver(argv[1]);

        // TODO: Shutdown all networkServer threads

        if (pthread_mutex_destroy(&printf_lock) != 0) {
            perror("ERROR pthread_mutex_destroy has failed for printf_lock");
            exit(1);
        }
    }
    else {
        printf("Usage:    daemon ifname\n");
        printf("  ifname:   Communication interface, e.g. eth1\n");
    }

    printf("Done\n"); // Some threads may still be running here; mutex would probably be good.
    return (0);
}

void ctrlC_handler(int signal){
    if(gotCtrlC==1) {
        //User is desperate. Kill it NOW.
        abort();
    }
    else {
        gotCtrlC = 1;
    }
}