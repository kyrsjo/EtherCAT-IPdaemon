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

#include <pwd.h>
#include <errno.h>

#include "networkServer.h"
#include "ecatDriver.h"

// Global data      ************************************************************************

// Set to 1 if we got a control+C interupt or if an IP client calls 'quit'
volatile sig_atomic_t gotCtrlC = 0;

// Lock for stdin/stdout/stderr (used once we get into threading)
pthread_mutex_t printf_lock;

// Lock for rootprivs; cannot start IP server before this is released.
pthread_mutex_t rootprivs_lock;

//Data from the config file
struct config_file_data config_file;

// File-global data ************************************************************************

pthread_t thread_communicate; // TCP/IP communications persistent thread

// Functions        ************************************************************************

int main(int argc, char *argv[]) {
    printf("EtherCat daemon, using SOEM\n");
    printf("*** For research purposes ONLY ***\n");

    if (argc == 2) {

        if (pthread_mutex_init(&rootprivs_lock, NULL) != 0) {
            perror("ERROR pthread_mutex_init has failed for printf_lock");
            exit(1);
        }
        pthread_mutex_lock(&rootprivs_lock);

        // Read config file
        if(parseConfigFile()) {
            fprintf(stderr,"Error while parsing config file.");
            exit(1);
        }

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
        pthread_mutex_unlock(&rootprivs_lock);
        if (pthread_mutex_destroy(&rootprivs_lock) != 0) {
            perror("ERROR pthread_mutex_destroy has failed for rootprivs_lock");
            exit(1);
        }


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

int parseConfigFile() {
    printf("Parsing config file '%s'...\n", CONFIGFILE_NAME);

    //config_file.dropPrivs_gid = 0;
    config_file.dropPrivs_username = malloc(100*sizeof(char));
    memset(config_file.dropPrivs_username,0,100);
    strncpy(config_file.dropPrivs_username, "nobody", 100);

    errno = 0;
    struct passwd* s_passw = getpwnam(config_file.dropPrivs_username);
    if (s_passw == NULL || errno) {
        fprintf(stderr,"Error when reading info for user '%s'", config_file.dropPrivs_username);
        return 1;
    }

    config_file.dropPrivs_uid = s_passw->pw_uid;
    config_file.dropPrivs_gid = s_passw->pw_gid;

    // Parse slave config

    // Done!
    printf("  Parse result:\n");
    printf("  dropPrivs_username = '%s'\n", config_file.dropPrivs_username);
    printf("  dropPrivs_uid      = '%d'\n", config_file.dropPrivs_uid);
    printf("  dropPrivs_gid      = '%d'\n", config_file.dropPrivs_gid);
    return 0; //success
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
