#ifndef ecatDriver_h
#define ecatDriver_h

#include "osal.h" //typedefs for uint 8 etc.

// Configuration   ************************************************************************
#ifndef EC_TIMEOUTMON // Allow setting from CMake
#define EC_TIMEOUTMON 500
#endif

#define PLC_waittime             5000 // How many us between each poll?
#define PLC_waittime_checkAlive 10000

// Data types       ************************************************************************

// Mapping between index:subindex to offsets into the global IOmap
struct mappings_PDO {
    //Static name of PDO
    uint16 slaveIdx;
    uint16 idx;
    uint8  subidx;
    //Pointer into IOmap
    int offset;
    int bitoff;
    uint8 bitlen;
    //Some metadata which may be useful
    int    dataType;
    char*  name;
    //it's a linked list -> pointer to the next one
    struct mappings_PDO* next;
};

// Global data      ************************************************************************

//The global IOmap into which all the process data is mapped
extern pthread_mutex_t IOmap_lock; //Lock for the IOmap;

extern boolean inOP;     // PLC is in mode OP
extern boolean updating; // IOmap is updating

//Note about these linked lists: The last entry is always completely set to 0, acting similar to a "string terminator \0".
// This lead to a less complex implementation than setting the last ->next==NULL.
extern struct mappings_PDO* mapping_out; // Outputs, i.e. setting of voltages, actuators etc.
extern struct mappings_PDO* mapping_in; // Inputs, i.e. reading of voltages, encoders, temperatures etc.

// Functions        ************************************************************************

// Periodically synchronize the PLC and the IOmap. Runs in it's own thread
void ecat_PLCdaemon();

// Convert an EtherCAT data type index to a string into the given buffer
// Returns *hstr.
char* dtype2string(uint16 dtype, char* hstr, int bufflen);

// Given a mapping into the IOmap, extract the data and convert to string into the given buffer
// Returns 1 on success, 0 on failure.
int PDOval2string(struct mappings_PDO* mapping, char* buff, int bufflen);


//Function to setup the mapping from slave/indx/subindx to memory address by interrogating the PLC.
// It is assumed that we can find everything over CoE, i.e. the slaves supprt the mailbox protocol.
// Return: 1 if all OK, 0 in case of error
int ecat_setup_mappings();
// Fill the linked lists mapping_out or mapping_in by interrogating the PLC
// Helper function for ecat_setup_mappings().
// Returns the current tail of the list, or NULL if there was an error
struct mappings_PDO* fill_mapping_list (uint16 slave, struct mappings_PDO* map_tail, uint16 PDOassign, size_t IOmapoffset);

//Function to extract the relevant link of a mappings_PDO list,
// typically either mapping_out or mapping_in,
// which contains data on where in the IOmap the PDO is located + metadata.
//Returns NULL if not found.
struct mappings_PDO* get_address(uint16 slaveID, uint16 idx, uint8 subidx, struct mappings_PDO* head);

//Initialize the EtherCAT PLC, setup the mappings, and start the daemon.
// Runs in it's own thread.
void ecat_driver(char* ifname);

//Function to check that all slaves are alive, and reinitialize them if needed.
// Runs in it's own thread.
OSAL_THREAD_FUNC ecat_check( void *ptr );

#endif