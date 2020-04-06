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
    printf("EtherCat IP daemon, using SOEM\n");
    printf("*** For research purposes ONLY ***\n");

    if (argc == 2) {

        if (pthread_mutex_init(&rootprivs_lock, NULL) != 0) {
            perror("ERROR pthread_mutex_init has failed for printf_lock");
            exit(1);
        }
        pthread_mutex_lock(&rootprivs_lock);

        // Read config file
        if(parseConfigFile()) {
            fprintf(stderr,"Error while parsing config file.\n");
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

        //DON'T ACTUALLY UNLOCK UNTILL THE NETWORK SERVERS ARE DEAD,
        // or we get a race condition where it can lock/unlock in main thread,
        // while network servers are still running. Mostly harmless, but still...
        //pthread_mutex_unlock(&rootprivs_lock);
        //if (pthread_mutex_destroy(&rootprivs_lock) != 0) {
        //    perror("ERROR pthread_mutex_destroy has failed for rootprivs_lock");
        //    exit(1);
        //}


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
    const size_t str_bufflen = 100; // !!!ALSO HARDCODED IN SSCANF (str_bufflen-1)!!!

    printf("Parsing config file '%s'...\n", CONFIGFILE_NAME);

    errno = 0;
    FILE* iFile = fopen(CONFIGFILE_NAME, "r");
    if (iFile == NULL || errno) {
        perror("Error on opening file");
        return 1;
    }

    // Initialize
    config_file.dropPrivs_username = NULL;
    config_file.allowQuit          = -1;
    config_file.iomap_size         = -1;

    char* parseBuff = malloc(str_bufflen*sizeof(char));
    int   parseInt1 = 0;
    int   parseInt2 = 0;
    int   parseInt3 = 0;

    char*  line = NULL;
    size_t line_len = 0;
    ssize_t readLen = 0;
    //Note: getline() handles allocations and reallocations
    while ( (readLen = getline(&line, &line_len, iFile)) != -1) {
        //PREPROCESSING OF LINE
        // Delete the final '\n' from a line
        char* tmp = strrchr(line,'\n');
        if (tmp != NULL) *tmp='\0';
        // Find first non-whitespace character
        tmp = line + strspn(line," \t");
        // Skip blank lines and comments
        if(tmp[0]=='\0' || tmp[0]=='!') continue;

        memset(parseBuff,0,str_bufflen);
        parseInt1 = 0;
        parseInt2 = 0;
        parseInt3 = 0;

        printf("Parsing: '%s'\n",tmp);

        // Parse !
        int gotHits = 0;

        gotHits = sscanf(tmp, "DROPPRIVS_USER %99s", parseBuff);
        if (gotHits>0) {
            if (config_file.dropPrivs_username != NULL) {
                fprintf(stderr, "Error in parseConfigFile(), got two DROPPRIVS_USER!\n");
                return 1;
            }
            config_file.dropPrivs_username = parseBuff;
            parseBuff = malloc(str_bufflen*sizeof(char));
            continue;
        }

        gotHits = sscanf(tmp, "ALLOWQUIT %s", parseBuff);
        if (gotHits>0) {
            if (config_file.allowQuit != -1) {
                fprintf(stderr, "Error in parseConfigFile(), got two ALLOWQUIT!\n");
                return 1;
            }

            if      ( strncmp(parseBuff, "YES", str_bufflen) == 0 ) {
                config_file.allowQuit = 1;
            }
            else if ( strncmp(parseBuff, "NO",  str_bufflen) == 0 ) {
                config_file.allowQuit = 0;
            }
            else {
                fprintf(stderr, "Error in parseConfigFile(), got invalid ALLOWQUIT '%s', expected 'YES' or 'NO'\n", parseBuff);
                return 1;
            }
            continue;
        }

        gotHits = sscanf(tmp, "IOMAP_SIZE %d", &parseInt1);
        if (gotHits>0) {
            if (config_file.iomap_size != -1) {
                fprintf(stderr, "Error in parseConfigFile(), got two IOMAP_SIZE!\n");
                return 1;
            }

            if (parseInt1 > 0) {
                config_file.iomap_size = parseInt1;
            }
            else {
                fprintf(stderr, "Error in parseConfigFile(), got invalid IOMAP_SIZE %d, expected > 0\n", parseInt1);
                return 1;
            }
            continue;
        }

        //Should never reach here:
        fprintf(stderr, "Error in parseConfigFile(), did not understand '%s'\n", tmp);
        return 1;
    }

    free(line);
    fclose(iFile);

    // Post-processing of parsed data & defaults
    if (config_file.dropPrivs_username == NULL) {
        config_file.dropPrivs_username = malloc(str_bufflen*sizeof(char));
        strncpy(config_file.dropPrivs_username,"nobody",str_bufflen);
    }

    errno = 0;
    struct passwd* s_passw = getpwnam(config_file.dropPrivs_username);
    if (s_passw == NULL || errno) {
        fprintf(stderr,"Error when reading info for user '%s'\n", config_file.dropPrivs_username);
        return 1;
    }

    config_file.dropPrivs_uid = s_passw->pw_uid;
    config_file.dropPrivs_gid = s_passw->pw_gid;

    if (config_file.allowQuit == -1) {
        config_file.allowQuit = 0; // Default: don't allow 'quit' command
    }

    if (config_file.iomap_size == -1) {
        config_file.iomap_size = 4096;
    }

    // Done!
    printf("  Parse result:\n");
    printf("  - dropPrivs_username = '%s'\n", config_file.dropPrivs_username);
    printf("  - dropPrivs_uid      = '%d'\n", config_file.dropPrivs_uid);
    printf("  - dropPrivs_gid      = '%d'\n", config_file.dropPrivs_gid);
    printf("  - allowQuit          =  %s\n",  config_file.allowQuit==1 ? "YES" : "NO");
    printf("  - iomap_size         =  %d\n",  config_file.iomap_size);

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
