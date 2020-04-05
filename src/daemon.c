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

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <stdlib.h>
#include <pthread.h>

#include "ethercat.h"

#define EC_TIMEOUTMON 500
#define NCYCLE 100000

//The global IOmap into which all the process data is mapped
char IOmap[4096]; //TODO: Verify map size
pthread_mutex_t lock; //Lock for the IOmap;

#include <signal.h>
//Set to 1 if we got a control+C interupt
//Also set to 1 if an IP client calls quit
volatile sig_atomic_t gotCtrlC = 0;

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
//Note about these linked lists: The last entry is always completely set to 0, acting similar to a "string terminator \0".
// This lead to a less complex implementation than setting the last ->next==NULL.
struct mappings_PDO* mapping_out = NULL; // Outputs, i.e. setting of voltages, actuators etc.
struct mappings_PDO* mapping_in  = NULL; // Inputs, i.e. reading of voltages, encoders, temperatures etc.

int expectedWKC;
boolean needlf;
volatile int wkc; // Don't think this really need volatile, as the CPU should guarantee cache-coherency
uint8 currentgroup = 0;

// Global flags
boolean inOP;
boolean updating;

//Persistent threads
OSAL_THREAD_HANDLE thread_PLCwatch;
OSAL_THREAD_HANDLE thread_communicate;

void ecat_PLCdaemon() {
    //This periodically synchronizes the PLC and the IOmap

    /* cyclic loop */
    while(TRUE) {

        pthread_mutex_lock(&lock);
        ec_send_processdata();
        wkc = ec_receive_processdata(EC_TIMEOUTRET);
        pthread_mutex_unlock(&lock);

        //Here we could in principle do some controlling

        osal_usleep(5000);

        if(gotCtrlC) break;
    }
    printf("Caught a control+c signal, shutting down now.\n");
}

// Copied from test/linux/slaveinfo/slaveinfo.c::dtype2string()
char* dtype2string(uint16 dtype, char* hstr) {
    switch(dtype) {
    case ECT_BOOLEAN:
        sprintf(hstr, "BOOLEAN");
        break;
    case ECT_INTEGER8:
        sprintf(hstr, "INTEGER8");
        break;
    case ECT_INTEGER16:
        sprintf(hstr, "INTEGER16");
        break;
    case ECT_INTEGER32:
        sprintf(hstr, "INTEGER32");
        break;
    case ECT_INTEGER24:
        sprintf(hstr, "INTEGER24");
        break;
    case ECT_INTEGER64:
        sprintf(hstr, "INTEGER64");
        break;
    case ECT_UNSIGNED8:
        sprintf(hstr, "UNSIGNED8");
        break;
    case ECT_UNSIGNED16:
        sprintf(hstr, "UNSIGNED16");
        break;
    case ECT_UNSIGNED32:
        sprintf(hstr, "UNSIGNED32");
        break;
    case ECT_UNSIGNED24:
        sprintf(hstr, "UNSIGNED24");
        break;
    case ECT_UNSIGNED64:
        sprintf(hstr, "UNSIGNED64");
        break;
    case ECT_REAL32:
        sprintf(hstr, "REAL32");
        break;
    case ECT_REAL64:
        sprintf(hstr, "REAL64");
        break;
    case ECT_BIT1:
        sprintf(hstr, "BIT1");
        break;
    case ECT_BIT2:
        sprintf(hstr, "BIT2");
        break;
    case ECT_BIT3:
        sprintf(hstr, "BIT3");
        break;
    case ECT_BIT4:
        sprintf(hstr, "BIT4");
        break;
    case ECT_BIT5:
        sprintf(hstr, "BIT5");
        break;
    case ECT_BIT6:
        sprintf(hstr, "BIT6");
        break;
    case ECT_BIT7:
        sprintf(hstr, "BIT7");
        break;
    case ECT_BIT8:
        sprintf(hstr, "BIT8");
        break;
    case ECT_VISIBLE_STRING:
        sprintf(hstr, "VISIBLE_STRING");
        break;
    case ECT_OCTET_STRING:
        sprintf(hstr, "OCTET_STRING");
        break;
    default:
        sprintf(hstr, "Type 0x%4.4X", dtype);
    }
    return hstr;
}

// Modified version of test/linux/slaveinfo/slaveinfo.c::SDO2string()
int PODval2string(struct mappings_PDO* mapping, char* buff, int bufflen) {
    //int l = sizeof(usdo) - 1, i;
   uint8 *u8;
   int8 *i8;
   uint16 *u16;
   int16 *i16;
   uint32 *u32;
   int32 *i32;
   uint64 *u64;
   int64 *i64;
   float *sr;
   double *dr;
   char es[32];

   //memset(&usdo, 0, 128);
   //ec_SDOread(slave, index, subidx, FALSE, &l, &usdo, EC_TIMEOUTRXM);
   //if (EcatError)
   //{
   //   return ec_elist2string();
   //}
   //else
   //{
   switch(mapping->dataType) {
   /*
   case ECT_BOOLEAN:
       u8 = (uint8*) &usdo[0];
       if (*u8) sprintf(hstr, "TRUE");
       else sprintf(hstr, "FALSE");
       break;
   */

   case ECT_INTEGER8:
       i8 = (int8*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%2.2x %d", *i8, *i8);
       break;

   case ECT_INTEGER16:
       i16 = (int16*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%4.4x %d", *i16, *i16);
       break;

   case ECT_INTEGER32:
   case ECT_INTEGER24:
       i32 = (int32*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%8.8x %d", *i32, *i32);
       break;

   case ECT_INTEGER64:
       i64 = (int64*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%16.16"PRIx64" %"PRId64, *i64, *i64);
       break;

   case ECT_UNSIGNED8:
       u8 = (uint8*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%2.2x %u", *u8, *u8);
       break;

   case ECT_UNSIGNED16:
       u16 = (uint16*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%4.4x %u", *u16, *u16);
       break;

   case ECT_UNSIGNED32:
   case ECT_UNSIGNED24:
       u32 = (uint32*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%8.8x %u", *u32, *u32);
       break;

   case ECT_UNSIGNED64:
       u64 = (uint64*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "0x%16.16"PRIx64" %"PRIu64, *u64, *u64);
       break;

   case ECT_REAL32:
       sr = (float*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "%f", *sr);
       break;

   case ECT_REAL64:
       dr = (double*) &(IOmap[mapping->offset]);
       if(mapping->bitoff != 0) goto allignmentError;
       snprintf(buff, bufflen, "%f", *dr);
       break;

   /*
   case ECT_BIT1:
   case ECT_BIT2:
   case ECT_BIT3:
   case ECT_BIT4:
   case ECT_BIT5:
   case ECT_BIT6:
   case ECT_BIT7:
   case ECT_BIT8:
       u8 = (uint8*) &usdo[0];
       sprintf(hstr, "0x%x", *u8);
       break;

   case ECT_VISIBLE_STRING:
       strcpy(hstr, usdo);
       break;

   case ECT_OCTET_STRING:
       hstr[0] = 0x00;
       for (i = 0 ; i < l ; i++) {
           sprintf(es, "0x%2.2x ", usdo[i]);
           strcat( hstr, es);
       }
       break;
   */

   default:
       snprintf(buff, bufflen, "Unknown type");
   }
   return 1; //success

 allignmentError:
   snprintf(buff, bufflen, "ALLIGNMENT ERROR");
   return 0; //failure
}


struct mappings_PDO* fill_mapping_list (uint16 slave, struct mappings_PDO* map_tail, uint16 PDOassign, size_t IOmapoffset) {
    // Fill the linked lists mapping_out and mapping_in
    // This code is is very close to test/linux/slaveinfo/slaveinfo.c::si_PDOassign()
    // Returns the current tail of the list, or NULL if there was an error

    char hstr[1024]; // String buffer for output

    // Note that I am assuming bitoffset = 0 at the beginning of every SM (aka. PDOassign).
    int bsize = 0;   // Size of SM in bits

    int rdl = 0;     // Length of last read

    //How many PDOs? (index PDOassign:0)
    uint16 rdat = 0;
    rdl = sizeof(rdat);
    wkc = ec_SDOread(slave, PDOassign, 0x00, FALSE, &rdl, &rdat, EC_TIMEOUTRXM);
    rdat = etohs(rdat);

    if ((wkc > 0) && (rdat > 0)) {
        uint16 nidx = rdat;

        //Loop over PDOs
        for (int idx_loop = 1; idx_loop <= nidx; idx_loop++) {
            //Get the index of the PDO
            rdl = sizeof(rdat); rdat = 0;
            wkc = ec_SDOread(slave, PDOassign, (uint8)idx_loop, FALSE, &rdl, &rdat, EC_TIMEOUTRXM);
            uint16 idx = etohs(rdat);

            if (idx > 0) {
                //Get the number of subindexes of this PDO
                uint8 subcnt = 0; rdl = sizeof(subcnt);
                wkc = ec_SDOread(slave,idx, 0x00, FALSE, &rdl, &subcnt, EC_TIMEOUTRXM);
                //uint16 subidx = subcnt;

                for (int subidx_loop = 1; subidx_loop <= subcnt; subidx_loop++) {
                    //Read the metadata for the PDO (mapped from SDO)
                    int32 rdat2 = 0; rdl = sizeof(rdat2);
                    wkc = ec_SDOread(slave, idx, (uint8)subidx_loop, FALSE, &rdl, &rdat2, EC_TIMEOUTRXM);
                    rdat2 = etohl(rdat2);
                    //Bitlen of SDO
                    uint8 bitlen = LO_BYTE(rdat2);
                    //Object indices
                    uint16 obj_idx    = (uint16)(rdat2 >> 16);
                    uint8  obj_subidx = (uint8)((rdat2 >> 8) & 0x000000ff);
                    //Compute offsets into global IOmap
                    int abs_offset = IOmapoffset + (bsize / 8);
                    int abs_bit = bsize % 8;

                    //Read object entry from dictionary if not a filler (index:subindex = 0x000:0x00)
                    if (obj_idx || obj_subidx) {
                        ec_ODlistt ODlist;
                        ec_OElistt OElist;

                        ODlist.Slave = slave;
                        ODlist.Index[0] = obj_idx;
                        OElist.Entries = 0;
                        wkc = 0;
                        wkc = ec_readOEsingle(0, obj_subidx, &ODlist, &OElist);

                        //Add data to the linked list!
                        map_tail->slaveIdx = slave;
                        map_tail->idx      = obj_idx;
                        map_tail->subidx   = obj_subidx;

                        map_tail->offset   = abs_offset;
                        map_tail->bitoff   = abs_bit;
                        map_tail->bitlen   = bitlen;

                        map_tail->dataType = OElist.DataType[obj_subidx];

                        size_t nameSize    = sizeof(char)*( strnlen(OElist.Name[obj_subidx],EC_MAXNAME) + 1 );
                        map_tail->name     = malloc(nameSize);
                        memset (map_tail->name, 0, nameSize);
                        strncpy(map_tail->name, OElist.Name[obj_subidx], nameSize);

                        //Last entry is always blank (trimmed off at the end)
                        map_tail->next = (struct mappings_PDO*) malloc(sizeof(struct mappings_PDO));
                        map_tail = map_tail->next;
                        memset(map_tail, 0, sizeof(sizeof(struct mappings_PDO)));

                        printf("[0x%4.4X.%1d] %d 0x%4.4X:0x%2.2X 0x%2.2X %-12s %s\n",
                               abs_offset, abs_bit, slave, obj_idx, obj_subidx, bitlen,
                               dtype2string(OElist.DataType[obj_subidx], hstr), OElist.Name[obj_subidx]);
                    }
                    bsize += bitlen;
                }
            }
        }
    }

    if (bsize%8 != 0) {
        printf("ERROR: bsize = %d of slave %d not divisible by 8.\n", bsize, slave);
        return NULL;
    }

    printf("\n");
    return map_tail;
}

int ecat_setup_mappings() {
    //Function to setup the mapping from slave/indx/subindx to memory address
    // It is assumed that we can find everything over CoE, i.e. the slaves supprt the mailbox protocol.
    // Return: 1 if all OK, 0 in case of error

    mapping_out = (struct mappings_PDO*) malloc(sizeof(struct mappings_PDO));
    struct mappings_PDO* mapping_out_tail = mapping_out;
    memset(mapping_out, 0, sizeof(sizeof(struct mappings_PDO)));

    mapping_in = (struct mappings_PDO*) malloc(sizeof(struct mappings_PDO));
    struct mappings_PDO* mapping_in_tail = mapping_in;
    memset(mapping_in, 0, sizeof(sizeof(struct mappings_PDO)));

    for(uint16 slave = 1 ; slave <= ec_slavecount ; slave++) {
        if (!(ec_slave[slave].mbx_proto & ECT_MBXPROT_COE)) {
            // Slave didn't support the CoE mailbox protocol.
            // The coupler needs this, so we can't completely ignore it.
            // This code is is very close to test/linux/slaveinfo/slaveinfo.c::si_map_sii()
            printf("Found SII setup of slave %d no action\n", slave);

            if (ec_slave[slave].Obytes || ec_slave[slave].Ibytes) {
                printf("ERROR in setup_mappings: slave %d is of type SII but not zero bytes.\n", slave);
                return 0;
            }
        }
        else {
            // Slave supports CAN over Ethernet (CoE) mailbox protocol.
            // Get number of SyncManager PDOs for this slave
            // This code is is very close to test/linux/slaveinfo/slaveinfo.c::si_map_sdo()
            printf("Found CoE setup of slave %d, reading PDOs...\n", slave);

            int nSM = 0;
            int rdl = sizeof(nSM);
            wkc = ec_SDOread(slave, ECT_SDO_SMCOMMTYPE, 0x00, FALSE, &rdl, &nSM, EC_TIMEOUTRXM);
            if ((wkc > 0) && (nSM > 2)) { // positive result from slave?
                if (nSM-1 > EC_MAXSM) {
                    printf("ERROR: nSM=%d for slave %d > EC_MAXSM = %d.\n", nSM, slave, EC_MAXSM);
                    printf("       This is not supported by daemon.c. \n");
                    return 0;
                }
                for (int iSM = 2 ; iSM < nSM ; iSM++) { // Only SM 2/3 are actually interesting for process data
                    // Check the communication type for this SM
                    uint8 tSM = 0; rdl = sizeof(tSM);
                    wkc = ec_SDOread(slave, ECT_SDO_SMCOMMTYPE, iSM+1, FALSE, &rdl, &tSM, EC_TIMEOUTRXM);
                    if (wkc > 0) {
                        if (iSM == 2) { // OUTPUTS
                            if (tSM != 3) {
                                fprintf(stderr,"ERROR: Got tSM=%d for iSM=%d while scanning slave %d\n",tSM,iSM,slave);
                                fprintf(stderr,"       This was not expected!\n");
                                return 0;
                            }
                            //Read the assigned RxPDO
                            size_t  IOmapoffset = (size_t)(ec_slave[slave].outputs - (uint8 *)&IOmap[0]);
                            mapping_out_tail = fill_mapping_list(slave, mapping_out_tail, ECT_SDO_PDOASSIGN + iSM, IOmapoffset);
                            if ( mapping_out_tail == NULL  ) {
                                printf("ERROR: unexpected behaviour of slave, implementation assumption was violated");
                                return 0;
                            }

                        }
                        else if (iSM == 3) { // INPUTS
                            if (tSM != 4) {
                                fprintf(stderr,"ERROR: Got tSM=%d for iSM=%d while scanning slave %d\n",tSM,iSM,slave);
                                fprintf(stderr,"       This was not expected!\n");
                                return 0;
                            }
                            //Read the assigned TxPDO
                            size_t  IOmapoffset = (size_t)(ec_slave[slave].inputs - (uint8 *)&IOmap[0]);
                            mapping_in_tail = fill_mapping_list(slave, mapping_in_tail, ECT_SDO_PDOASSIGN + iSM, IOmapoffset);
                            if ( mapping_in_tail == NULL ) {
                                printf("ERROR: unexpected behaviour of slave, implementation assumption was violated");
                                return 0;
                            }

                        }
                        else { // Should never happen...
                            fprintf(stderr,"ERROR: Got iSM=%d (tSM=%d) while scanning slave %d\n",iSM, tSM, slave);
                            fprintf(stderr,"       This was not expected!\n");
                            return 0;
                            break;
                        }
                    }

                }

            }
        }
    }

    return 1; //Success!
}
struct mappings_PDO* get_address(uint16 slaveID, uint16 idx, uint8 subidx, struct mappings_PDO* head){
    //Function to extract the relevant link of a mappings_PDO list,
    // typically either mapping_out or mapping_in,
    // which contains data on where in the IOmap the PDO is located + metadata.
    //Returns NULL if not found.

    struct mappings_PDO* current = NULL;

    current = head;
    do {
        if (slaveID == current->slaveIdx &&
            idx == current->idx          &&
            subidx == current->subidx      )
            return current;

        current = current->next;

    } while (current->bitlen > 0);

    return NULL; // Nothing was found.
}

void ecat_initialize(char* ifname) {
    int chk;

    int slaveID = 2;

    needlf = FALSE;
    inOP = FALSE;

    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(stderr,"\n mutex init has failed\n");
        exit(1);
    }

    printf("Starting daemon\n");

    /* initialise SOEM, bind socket to ifname */
    if (ec_init(ifname)) {
        printf("ec_init on %s succeeded.\n",ifname);
        /* find and auto-config slaves */

        if ( ec_config_init(FALSE) > 0 ) {
            printf("%d slaves found and configured.\n",ec_slavecount);
            if (slaveID > ec_slavecount) {
                ec_close();
                fprintf(stderr, "ERROR: slavecount < slaveID. Quitting.\n");
                exit(1);
            }

            ec_config_map(&IOmap); // fills ec_slave and more

            ec_configdc();

            printf("Slaves mapped, state to SAFE_OP.\n");
            /* wait for all slaves to reach SAFE_OP state */
            ec_statecheck(0, EC_STATE_SAFE_OP,  EC_TIMEOUTSTATE * 4);

            printf("segments : %d : %d %d %d %d\n",ec_group[0].nsegments ,ec_group[0].IOsegment[0],ec_group[0].IOsegment[1],ec_group[0].IOsegment[2],ec_group[0].IOsegment[3]);

            if(!ecat_setup_mappings()) {
                fprintf(stderr, "Error in setup_mappings()\n");
                exit(1);
            }

            printf("Request operational state for all slaves\n");
            expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
            printf("Calculated workcounter %d\n", expectedWKC);
            ec_slave[0].state = EC_STATE_OPERATIONAL;
            /* send one valid process data to make outputs in slaves happy*/
            ec_send_processdata();
            ec_receive_processdata(EC_TIMEOUTRET);
            /* request OP state for all slaves */
            ec_writestate(0);
            chk = 200;
            /* wait for all slaves to reach OP state */
            do {
                ec_send_processdata();
                ec_receive_processdata(EC_TIMEOUTRET);
                ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
            }
            while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));

            if (ec_slave[0].state == EC_STATE_OPERATIONAL ) {
                printf("Operational state reached for all slaves.\n");
                inOP = TRUE;
                updating = TRUE;
                ecat_PLCdaemon();

                inOP = FALSE;
            }
            else {
                printf("Not all slaves reached operational state.\n");
                ec_readstate();
                for(int i = 1; i<=ec_slavecount ; i++) {
                    if(ec_slave[i].state != EC_STATE_OPERATIONAL) {
                        printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                               i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
                    }
                }
            }
            printf("\nRequest init state for all slaves\n");
            ec_slave[0].state = EC_STATE_INIT;
            /* request INIT state for all slaves */
            ec_writestate(0);
        }
        else {
            printf("No slaves found!\n");
        }
        printf("End simple test, close SOEM socket\n");
        /* stop SOEM, close socket */
        ec_close();
    }
    else {
        printf("No socket connection on %s\nPlease excecute as root!\n",ifname);
    }

}


//Function to check that all slaves are alive,
// and reinitialize them if needed.
OSAL_THREAD_FUNC ecat_check( void *ptr ) {
    (void)ptr; // Not used, reference it to quiet down the compiler

    while(1) {
        if( inOP && ((wkc < expectedWKC) || ec_group[currentgroup].docheckstate)) {
            if (needlf) {
               needlf = FALSE;
               printf("\n");
            }
            /* one ore more slaves are not responding */
            updating = FALSE;
            ec_group[currentgroup].docheckstate = FALSE;
            ec_readstate();
            for (uint16 slave = 1; slave <= ec_slavecount; slave++) {
               if ((ec_slave[slave].group == currentgroup) && (ec_slave[slave].state != EC_STATE_OPERATIONAL)) {
                  ec_group[currentgroup].docheckstate = TRUE;
                  if (ec_slave[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
                     printf("ERROR : slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
                     ec_slave[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
                     ec_writestate(slave);
                  }
                  else if(ec_slave[slave].state == EC_STATE_SAFE_OP) {
                     printf("WARNING : slave %d is in SAFE_OP, change to OPERATIONAL.\n", slave);
                     ec_slave[slave].state = EC_STATE_OPERATIONAL;
                     ec_writestate(slave);
                  }
                  else if(ec_slave[slave].state > EC_STATE_NONE) {
                     if (ec_reconfig_slave(slave, EC_TIMEOUTMON)) {
                        ec_slave[slave].islost = FALSE;
                        printf("MESSAGE : slave %d reconfigured\n",slave);
                     }
                  }
                  else if(!ec_slave[slave].islost) {
                     /* re-check state */
                     ec_statecheck(slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                     if (ec_slave[slave].state == EC_STATE_NONE) {
                        ec_slave[slave].islost = TRUE;
                        printf("ERROR : slave %d lost\n",slave);
                     }
                  }
               }
               if (ec_slave[slave].islost) {
                  if(ec_slave[slave].state == EC_STATE_NONE) {
                     if (ec_recover_slave(slave, EC_TIMEOUTMON)) {
                        ec_slave[slave].islost = FALSE;
                        printf("MESSAGE : slave %d recovered\n",slave);
                     }
                  }
                  else {
                     ec_slave[slave].islost = FALSE;
                     printf("MESSAGE : slave %d found\n",slave);
                  }
               }
            }
            if(!ec_group[currentgroup].docheckstate) {
                printf("OK : all slaves resumed OPERATIONAL.\n");
                updating = TRUE;
            }
        }
        osal_usleep(10000);
    }
}

//#include <stdio.h>
//#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
//#include <netdb.h>
#include <arpa/inet.h>
//Socket on the server
struct sockaddr_in servaddr;
int sockfd;
#define PORT 4200
// Array of server connection slots
#define NUMIPSERVERS 50
struct IPserverThreads {
    struct sockaddr_in client;
    int connfd;

    pthread_t thread;
    int  ipServerNum;
    char inUse;
};
struct IPserverThreads IPservers[NUMIPSERVERS];

#define BUFFLEN 1024
int writeMapping(char* buff_out, struct mappings_PDO* mapping, int connfd) {
    //Helper function for chatThread()

    char hstr[BUFFLEN]; // String buffer for conversion functions
    memset(hstr,0,BUFFLEN);

    int numChars = snprintf(buff_out, BUFFLEN,
                            "  [0x%4.4X.%1d] %d:0x%4.4X:0x%2.2X 0x%2.2X %-12s %s\n",
                            mapping->offset, mapping->bitoff,
                            mapping->slaveIdx, mapping->idx, mapping->subidx,
                            mapping->bitlen, dtype2string(mapping->dataType, hstr), mapping->name);
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
        int numBytes = read(myThread->connfd, buff_in, BUFFLEN);
        if (numBytes >= BUFFLEN) {
            //Note: Last byte in buff should always be \0.
            printf("ERROR, message too long");
            goto endcom;
        }
        printf("GOT: (%d) '%s'\n", strlen(buff_in), buff_in);

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
            printf("quit from slot %d address %s \n", myThread->ipServerNum, inet_ntoa(myThread->client.sin_addr));
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

            if (buffUsed >= BUFFLEN) {
                printf("ERROR: buff_out overextended\n");
                exit(1);
            }

            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out, 0, BUFFLEN);
        }
        else if (!strncmp(buff_in, "dump",    4))  {  // dump
            //dump the current raw IOmap content
            if (!inOP || !updating) {
                strncpy(buff_out, "err: not inOP or not updating\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out, 0, BUFFLEN);
            }
            else {
                pthread_mutex_lock(&lock);

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
                        printf("ERROR: buff_out overextended\n");
                        exit(1);
                    }
                    write(myThread->connfd, buff_out, BUFFLEN);
                    memset(buff_out, 0, BUFFLEN);
                }
                pthread_mutex_unlock(&lock);
            }
        }
        else if (!strncmp(buff_in, "meta all", 8))  {  // meta all
            //Metadata about all slaves/indexes/subindexes
            struct mappings_PDO* mapping_active;

            strncpy(buff_out, "OUTPUTS:\n", BUFFLEN);
            write(myThread->connfd, buff_out, BUFFLEN);

            mapping_active = mapping_out;
            while (mapping_active->bitlen > 0) {
                writeMapping(buff_out, mapping_active, myThread->connfd);
                mapping_active = mapping_active->next;
            }

            strncpy(buff_out, "INPUTS:\n", BUFFLEN);
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
                printf("%d:%x:%x\n", slave,idx,subidx);
            }
            else {
                strncpy(buff_out, "err: meta got bad args\n", BUFFLEN);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
            }

            //struct mappings_PDO* data = get_address(2, 0x6000, 0x11, mapping_in);
            //printf("[0x%4.4X.%1d] 0x%2.2X; VALUE=", data->offset, data->bitoff, data->bitlen);
            //int16* i16 = NULL;
            //i16 = (int16*) &(IOmap[data->offset]);
            //printf("%d\n", *i16);

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

            printf("%d:%x:%x\n", slave,idx,subidx);

            struct mappings_PDO* dataMapping = get_address(slave, idx, subidx, mapping_in);
            if (dataMapping == NULL) {
                snprintf(buff_out, BUFFLEN, "err: POD address %d:%x:%x not recognized (searched for inputs)\n", slave,idx,subidx);
                write(myThread->connfd, buff_out, BUFFLEN);
                memset(buff_out,0,BUFFLEN);
                goto donecmds;
            }

            pthread_mutex_lock(&lock);

            PODval2string(dataMapping, hstr, BUFFLEN);
            pthread_mutex_unlock(&lock);

            snprintf(buff_out, BUFFLEN, "  %s\n", hstr);
            write(myThread->connfd, buff_out, BUFFLEN);
            memset(hstr,0,BUFFLEN);
            memset(buff_out,0,BUFFLEN);

        }
        else {                                      // (unknown command)
            snprintf(buff_out, BUFFLEN, "err: unknown command '%s'\n",buff_in);
            write(myThread->connfd, buff_out, BUFFLEN);
            memset(buff_out,0,BUFFLEN);
        }

    donecmds: // Escape from inside an input handler

        //Tell the client that the response is finished
        strncpy(buff_out,"ok\n",BUFFLEN);
        write(myThread->connfd, buff_out, BUFFLEN);
        memset(buff_out,0,BUFFLEN);

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

    printf("Finished: -- slot %d disconnecting from %s \n", myThread->ipServerNum, inet_ntoa(myThread->client.sin_addr));
    memset(buff_out, 0, BUFFLEN);

    strncpy(buff_out, "bye\n", BUFFLEN);
    write(myThread->connfd, buff_out, 4);

    close(myThread->connfd);
    myThread->inUse = 0;
}

OSAL_THREAD_FUNC mainIPserver( void* ptr ) {
    //Print data in memory when hitting ENTER

    // IP connection handling and server spinup
    // Inspired by https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/

    (void)ptr; // Not used, reference it to quiet down the compiler

    //Initialize IPservers array
    memset(IPservers, 0, sizeof(struct IPserverThreads)*NUMIPSERVERS);
    /*    for (int i = 0; i < NUMIPSERVERS; i++) {
        IPservers[i].inUse = 0;
        memset(&(IPservers[i].servaddr), 0, sizeof(struct sockaddr_in));
        IPservers[i].sockfd = 0;
    }
    */

    //Create the server socket...
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("ERROR when opening socket");
        exit(1);
    }

    int enableReuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enableReuse, sizeof(int)) < 0){
        printf("WARNING: setsockopt(SO_REUSEADDR) failed");
    }

    //Set server IP and port
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0) {
        perror("ERROR: Socket bind failed");
        fprintf(stderr, "if 'netstat | grep %d' shows TIME_WAIT, please wait for the OS timeout to finish\n", PORT);
        close(sockfd);
        exit(1);
    }

    //Listen to the socket...
    if ((listen(sockfd, 5)) != 0) {
        perror("ERROR: Socket listen failed");
        exit(1);
    }
    printf("listen OK\n");


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
            printf("Too many clients!\n");
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

        printf("IP server slot %d connected to host %s \n", ipServerNum, inet_ntoa(IPservers[ipServerNum].client.sin_addr));

        //Here using Linux pthreads, not OSAL,
        // because we want to do more than just creating the threads.
        // When done, these threads close their connection and set inUse = 0.
        pthread_create(&(IPservers[ipServerNum].thread), NULL, (void*) &chatThread, (void*) &(IPservers[ipServerNum]));

    noSock:
        /*
        if (inOP) {
            printf("Hit ENTER to print current state\n");
            getchar();
            pthread_mutex_lock(&lock);
            //printf("Processdata cycle %05d/%d, WKC %d ;", i, NCYCLE, wkc);
            printf(" T:%" PRId64 "; ",ec_DCtime);

            for (uint16 slave = 1; slave <= ec_slavecount; slave++) {
                printf("slave[%d]:",slave);
                printf(" O:");
                int nChars = ec_slave[slave].Obytes;
                if (nChars==0 && ec_slave[slave].Obytes > 0) nChars = 1;
                for(int j = 0 ; j < nChars ; j++) {
                    printf(" %2.2x", *(ec_slave[slave].outputs + j));
                }

                printf(" I:");
                nChars = ec_slave[slave].Ibytes;
                if (nChars==0 && ec_slave[slave].Ibytes > 0) nChars = 1;
                for(int j = 0 ; j < nChars ; j++) {
                    printf(" %2.2x", *(ec_slave[slave].inputs + j));
                }

                printf("; ");

            }
            printf("\r");
            printf("\n");

            //Try to get the address the VALUE of the first sensor
            struct mappings_PDO* data = get_address(2, 0x6000, 0x11, mapping_in);
            printf("[0x%4.4X.%1d] 0x%2.2X; VALUE=", data->offset, data->bitoff, data->bitlen);
            int16* i16 = NULL;
            i16 = (int16*) &(IOmap[data->offset]);
            printf("%d\n", *i16);

            //needlf = TRUE;

            pthread_mutex_unlock(&lock);
        }
        */
        osal_usleep(10000);
    }
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
        osal_thread_create(&thread_communicate, 128000, (void*) &mainIPserver, (void*) &ctime);

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
