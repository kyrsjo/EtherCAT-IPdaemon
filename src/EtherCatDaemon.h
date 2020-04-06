#ifndef EtherCatDaemon_h
#define EtherCatDaemon_h

#include <signal.h>
#include <pthread.h>

#include <sys/types.h>

// Configuration    ************************************************************************

#define CONFIGFILE_NAME "config.txt"

// Data types       ************************************************************************
struct config_file_data {
    //char wasParsed; // true (1) or false (0)

    //Drop privs to these UID/GID
    char* dropPrivs_username;
    uid_t dropPrivs_uid;
    gid_t dropPrivs_gid;

    char allowQuit; // true(1) or false(0)

    int iomap_size; //Size of IOmap allocation [bytes]

};

// Global data      ************************************************************************

//Data from the config file
extern struct config_file_data config_file;

// Set to 1 if we got a control+C interupt or if an IP client calls 'quit'
extern volatile sig_atomic_t gotCtrlC;

//Lock for printf;
// Note: If used together with IOmap_lock,
// the IOmap lock should always be the "outer" lock in order to avoid deadlocks.
// Ideally, printf_lock should be grabbed just before printing and released just after.
extern pthread_mutex_t printf_lock;

//Barrier for "has dropped root privs";
//This is set during initialization, and dropped once the RAW socket is opened.
extern pthread_mutex_t rootprivs_lock;


// Functions        ************************************************************************
void ctrlC_handler(int signal);

int parseConfigFile();

#endif