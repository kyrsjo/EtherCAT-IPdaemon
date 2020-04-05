#ifndef EtherCatDaemon_h
#define EtherCatDaemon_h

#include <signal.h>
#include <pthread.h>

// Configuration   ************************************************************************

// Data types       ************************************************************************

// Global data      ************************************************************************

// Set to 1 if we got a control+C interupt or if an IP client calls 'quit'
extern volatile sig_atomic_t gotCtrlC;

//Lock for printf;
// Note: If used together with IOmap_lock,
// the IOmap lock should always be the "outer" lock in order to avoid deadlocks.
// Ideally, printf_lock should be grabbed just before printing and released just after.
extern pthread_mutex_t printf_lock; 

// Functions        ************************************************************************
void ctrlC_handler(int signal);

#endif