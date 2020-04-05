#ifndef EtherCatDaemon_h
#define EtherCatDaemon_h

#include <signal.h>

// Configuration   ************************************************************************

// Data types       ************************************************************************

// Global data      ************************************************************************

// Set to 1 if we got a control+C interupt or if an IP client calls 'quit'
extern volatile sig_atomic_t gotCtrlC;

// Functions        ************************************************************************
void ctrlC_handler(int signal);

#endif