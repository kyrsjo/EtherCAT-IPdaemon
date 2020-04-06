#include "ecatDriver.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include "ethercat.h"

#include "EtherCatDaemon.h"

// Global data      ************************************************************************
pthread_mutex_t IOmap_lock; //Lock for the IOmap; defined in ecatDriver.h

volatile boolean inOP;     // PLC is in mode OP
volatile boolean updating; // IOmap is updating

//Note about these linked lists: The last entry is always completely set to 0, acting similar to a "string terminator \0".
// This lead to a less complex implementation than setting the last ->next==NULL.
struct mappings_PDO* mapping_out = NULL; // Outputs, i.e. setting of voltages, actuators etc.
struct mappings_PDO* mapping_in  = NULL; // Inputs, i.e. reading of voltages, encoders, temperatures etc.

// File-global data ************************************************************************
char* IOmap;

OSAL_THREAD_HANDLE thread_PLCwatch; // Slave error handling (disconnect etc.)

// Work counter status, for error checking
volatile int wkc;
int expectedWKC;

uint8 currentgroup = 0;

// Functions        ************************************************************************

void ecat_PLCdaemon() {
    //This periodically synchronizes the PLC and the IOmap

    /* create thread to handle slave error handling in OP */
    //Note: This is technically a library bug;
    // function pointers should not be declared as void*!
    // https://isocpp.org/wiki/faq/pointers-to-members#cant-cvt-fnptr-to-voidptr
    osal_thread_create(&thread_PLCwatch,    128000, (void*) &ecat_check,    (void*) &ctime);

    /* cyclic loop */
    while(1) {

        pthread_mutex_lock(&IOmap_lock);
        ec_send_processdata();
        wkc = ec_receive_processdata(EC_TIMEOUTRET);
        pthread_mutex_unlock(&IOmap_lock);

        //Here we could in principle do some controlling

        osal_usleep(PLC_waittime);

        if(gotCtrlC) break;
    }
    pthread_mutex_lock(&printf_lock);
    printf("Caught a control+c signal, shutting down now.\n");
    pthread_mutex_unlock(&printf_lock);
}

// Adapted from SOEM/test/linux/slaveinfo/slaveinfo.c::dtype2string()
char* dtype2string(uint16 dtype, char* hstr, int bufflen) {
    switch(dtype) {
    case ECT_BOOLEAN:
        snprintf(hstr, bufflen, "BOOLEAN");
        break;
    case ECT_INTEGER8:
        snprintf(hstr, bufflen, "INTEGER8");
        break;
    case ECT_INTEGER16:
        snprintf(hstr, bufflen, "INTEGER16");
        break;
    case ECT_INTEGER32:
        snprintf(hstr, bufflen, "INTEGER32");
        break;
    case ECT_INTEGER24:
        snprintf(hstr, bufflen, "INTEGER24");
        break;
    case ECT_INTEGER64:
        snprintf(hstr, bufflen, "INTEGER64");
        break;
    case ECT_UNSIGNED8:
        snprintf(hstr, bufflen, "UNSIGNED8");
        break;
    case ECT_UNSIGNED16:
        snprintf(hstr, bufflen, "UNSIGNED16");
        break;
    case ECT_UNSIGNED32:
        snprintf(hstr, bufflen, "UNSIGNED32");
        break;
    case ECT_UNSIGNED24:
        snprintf(hstr, bufflen, "UNSIGNED24");
        break;
    case ECT_UNSIGNED64:
        snprintf(hstr, bufflen, "UNSIGNED64");
        break;
    case ECT_REAL32:
        snprintf(hstr, bufflen, "REAL32");
        break;
    case ECT_REAL64:
        snprintf(hstr, bufflen, "REAL64");
        break;
    case ECT_BIT1:
        snprintf(hstr, bufflen, "BIT1");
        break;
    case ECT_BIT2:
        snprintf(hstr, bufflen, "BIT2");
        break;
    case ECT_BIT3:
        snprintf(hstr, bufflen, "BIT3");
        break;
    case ECT_BIT4:
        snprintf(hstr, bufflen, "BIT4");
        break;
    case ECT_BIT5:
        snprintf(hstr, bufflen, "BIT5");
        break;
    case ECT_BIT6:
        snprintf(hstr, bufflen, "BIT6");
        break;
    case ECT_BIT7:
        snprintf(hstr, bufflen, "BIT7");
        break;
    case ECT_BIT8:
        snprintf(hstr, bufflen, "BIT8");
        break;
    case ECT_VISIBLE_STRING:
        snprintf(hstr, bufflen, "VISIBLE_STRING");
        break;
    case ECT_OCTET_STRING:
        snprintf(hstr, bufflen, "OCTET_STRING");
        break;
    default:
        snprintf(hstr, bufflen, "Type 0x%4.4X", dtype);
    }
    return hstr;
}

// Modified version of SOEM/test/linux/slaveinfo/slaveinfo.c::SDO2string()
int PDOval2string(struct mappings_PDO* mapping, char* buff, int bufflen) {

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

    const int es_len = 32;
    char es[es_len];

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

    //TODO!!!!
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
    // Fill the linked lists mapping_out and mapping_in; helper function for ecat_setup_mappings()
    // This code is is very close to SOEM/test/linux/slaveinfo/slaveinfo.c::si_PDOassign()
    // Returns the current tail of the list, or NULL if there was an error

    // Note:: Assumes printf_lock to be already grabbed by the calling function

    const int bufflen = 1024;
    char hstr[bufflen]; // String buffer for output

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
                        memset(map_tail, 0, sizeof(struct mappings_PDO));

                        printf("[0x%4.4X.%1d] %d 0x%4.4X:0x%2.2X 0x%2.2X %-12s %s\n",
                               abs_offset, abs_bit, slave, obj_idx, obj_subidx, bitlen,
                               dtype2string(OElist.DataType[obj_subidx], hstr, bufflen), OElist.Name[obj_subidx]);
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
    //printf("\n");
    return map_tail;
}

int ecat_setup_mappings() {
    //Function to setup the mapping from slave/indx/subindx to memory address
    // It is assumed that we can find everything over CoE, i.e. the slaves supprt the mailbox protocol.
    // Return: 1 if all OK, 0 in case of error

    // Note: IOmapLock is assumed to be grabbed by calling thread
    // Note: This thread grabs the printf_lock. Any called functions should NOT grab this lock!

    pthread_mutex_lock(&printf_lock);

    mapping_out = (struct mappings_PDO*) malloc(sizeof(struct mappings_PDO));
    struct mappings_PDO* mapping_out_tail = mapping_out;
    memset(mapping_out, 0, sizeof(struct mappings_PDO));

    mapping_in = (struct mappings_PDO*) malloc(sizeof(struct mappings_PDO));
    struct mappings_PDO* mapping_in_tail = mapping_in;
    memset(mapping_in, 0, sizeof(struct mappings_PDO));

    for(uint16 slave = 1 ; slave <= ec_slavecount ; slave++) {
        if (!(ec_slave[slave].mbx_proto & ECT_MBXPROT_COE)) {
            // Slave didn't support the CoE mailbox protocol.
            // The coupler needs this, so we can't completely ignore it.
            // This code is is very close to SOEM/test/linux/slaveinfo/slaveinfo.c::si_map_sii()
            printf("Found SII setup of slave %d; no action.\n", slave);
            if (ec_slave[slave].Obytes || ec_slave[slave].Ibytes) {
                printf("ERROR in setup_mappings: slave %d is of type SII but not zero bytes.\n", slave);
                return 0;
            }
        }
        else {
            // Slave supports CAN over Ethernet (CoE) mailbox protocol.
            // Get number of SyncManager PDOs for this slave
            // This code is is very close to SOEM/test/linux/slaveinfo/slaveinfo.c::si_map_sdo()
            printf("Found CoE setup of slave %d; reading PDOs...\n", slave);

            int nSM = 0;
            int rdl = sizeof(nSM);
            wkc = ec_SDOread(slave, ECT_SDO_SMCOMMTYPE, 0x00, FALSE, &rdl, &nSM, EC_TIMEOUTRXM);
            if ((wkc > 0) && (nSM > 2)) { // positive result from slave?
                if (nSM-1 > EC_MAXSM) {
                    printf("ERROR: nSM=%d for slave %d > EC_MAXSM = %d.\n", nSM, slave, EC_MAXSM);
                    printf("       This is not supported by daemon.c. \n");
                    goto return_fail;
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
                                goto return_fail;
                            }
                            //Read the assigned RxPDO
                            size_t  IOmapoffset = (size_t)(ec_slave[slave].outputs - (uint8 *)&IOmap[0]);
                            printf("OUTPUTS:\n");
                            mapping_out_tail = fill_mapping_list(slave, mapping_out_tail, ECT_SDO_PDOASSIGN + iSM, IOmapoffset);
                            if ( mapping_out_tail == NULL  ) {
                                printf("ERROR: unexpected behaviour of slave, implementation assumption was violated");
                                goto return_fail;
                            }

                        }
                        else if (iSM == 3) { // INPUTS
                            if (tSM != 4) {
                                fprintf(stderr,"ERROR: Got tSM=%d for iSM=%d while scanning slave %d\n",tSM,iSM,slave);
                                fprintf(stderr,"       This was not expected!\n");
                                goto return_fail;
                            }
                            //Read the assigned TxPDO
                            size_t  IOmapoffset = (size_t)(ec_slave[slave].inputs - (uint8 *)&IOmap[0]);
                            printf("INPUTS:\n");
                            mapping_in_tail = fill_mapping_list(slave, mapping_in_tail, ECT_SDO_PDOASSIGN + iSM, IOmapoffset);
                            if ( mapping_in_tail == NULL ) {
                                printf("ERROR: unexpected behaviour of slave, implementation assumption was violated");
                                goto return_fail;
                            }

                        }
                        else { // Should never happen...
                            fprintf(stderr,"ERROR: Got iSM=%d (tSM=%d) while scanning slave %d\n",iSM, tSM, slave);
                            fprintf(stderr,"       This was not expected!\n");
                            goto return_fail;
                        }
                    }

                }

            }
        }
    }

    pthread_mutex_unlock(&printf_lock);
    return 1; //Success!

return_fail:
    pthread_mutex_unlock(&printf_lock);
    return 0; // Failure

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

void ecat_driver(char* ifname) {
    int chk;

    //needlf = FALSE;
    inOP = FALSE;

    if (pthread_mutex_init(&IOmap_lock, NULL) != 0) {
        pthread_mutex_lock(&printf_lock);
        perror("ERROR pthread_mutex_init has failed for IOmap_lock");
        pthread_mutex_unlock(&printf_lock);
        exit(1);
    }

    pthread_mutex_lock(&printf_lock);
    printf("Starting driver...\n");
    pthread_mutex_unlock(&printf_lock);

    /* initialise SOEM, bind socket to ifname */
    if (ec_init(ifname)) {
        pthread_mutex_lock(&printf_lock);
        printf("ec_init on %s succeeded.\n",ifname);
        pthread_mutex_unlock(&printf_lock);

        /*  Drop superuser privileges in correct order */
        pthread_mutex_lock(&printf_lock);
        printf("Dropping root privilegies...\n");
        pthread_mutex_unlock(&printf_lock);
        if (setgid(config_file.dropPrivs_gid) == -1) {
            pthread_mutex_lock(&printf_lock);
            perror("Error during setgit()");
            pthread_mutex_unlock(&printf_lock);
            exit(1);
        }
        if (setuid(config_file.dropPrivs_uid) == -1) {
            pthread_mutex_lock(&printf_lock);
            perror("Error during setuid()");
            pthread_mutex_unlock(&printf_lock);
            exit(1);
        }
        //Unlock the rootprivs_lock; the TCP/IP server is now safe to start
        pthread_mutex_unlock(&rootprivs_lock);

        pthread_mutex_lock(&printf_lock);
        printf("Now running as '%s'.\n",config_file.dropPrivs_username);
        pthread_mutex_unlock(&printf_lock);


        /* find and auto-config slaves */
        pthread_mutex_lock(&IOmap_lock); // Grab this lock untill we've done initializing
        if ( ec_config_init(FALSE) > 0 ) {
            pthread_mutex_lock(&printf_lock);
            printf("%d slaves found and configured.\n",ec_slavecount);
            pthread_mutex_unlock(&printf_lock);

            IOmap = malloc(config_file.iomap_size*sizeof(char));
            memset(IOmap,0,config_file.iomap_size); // Expected to be initialized on first ec_send_processdata()

            int iomap_size = ec_config_map(IOmap); // fills ec_slave and more.
            pthread_mutex_lock(&printf_lock);
            printf("Generated IOmap has size %d, configured iomap_size = %d\n",
                    iomap_size, config_file.iomap_size);
            if (iomap_size > config_file.iomap_size) {
                fprintf(stderr,
                    "Error in setup_mapping(): generated IOmap size  > configured iomap_size\n");
                pthread_mutex_unlock(&printf_lock);
                exit(1);
            }
            pthread_mutex_unlock(&printf_lock);

            ec_configdc();

            pthread_mutex_lock(&printf_lock);
            printf("Slaves mapped, state to SAFE_OP.\n");
            pthread_mutex_unlock(&printf_lock);
            /* wait for all slaves to reach SAFE_OP state */
            ec_statecheck(0, EC_STATE_SAFE_OP,  EC_TIMEOUTSTATE * 4);

            //printf("segments : %d : %d %d %d %d\n",ec_group[0].nsegments ,ec_group[0].IOsegment[0],ec_group[0].IOsegment[1],ec_group[0].IOsegment[2],ec_group[0].IOsegment[3]);

            pthread_mutex_lock(&printf_lock);
            printf("\n");
            printf("PDO mappings:\n");
            pthread_mutex_unlock(&printf_lock);
            if(!ecat_setup_mappings()) {
                pthread_mutex_lock(&printf_lock);
                fprintf(stderr, "Error in setup_mappings()\n");
                pthread_mutex_unlock(&printf_lock);
                exit(1);
            }

            pthread_mutex_lock(&printf_lock);
            printf("\n");
            printf("Request operational state for all slaves\n");
            expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
            printf("Calculated workcounter %d\n", expectedWKC);
            pthread_mutex_unlock(&printf_lock);
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
                pthread_mutex_lock(&printf_lock);
                printf("Operational state reached for all slaves.\n");
                printf("\n");
                pthread_mutex_unlock(&printf_lock);

                inOP = TRUE;
                updating = TRUE;
                pthread_mutex_unlock(&IOmap_lock);

                ecat_PLCdaemon(); // !!! HERE WE ARE IN OPERATION; WILL STAY IN THIS FUNCTION UNTIL QUITTING !!!

                inOP = FALSE;
            }
            else {
                pthread_mutex_lock(&printf_lock);
                printf("Not all slaves reached operational state.\n");
                pthread_mutex_unlock(&printf_lock);

                ec_readstate();
                pthread_mutex_lock(&printf_lock);
                for(int i = 1; i<=ec_slavecount ; i++) {
                    if(ec_slave[i].state != EC_STATE_OPERATIONAL) {
                        printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                               i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
                    }
                }
                pthread_mutex_unlock(&printf_lock);

                pthread_mutex_unlock(&IOmap_lock);

            }

            pthread_mutex_lock(&printf_lock);
            printf("\nRequest init state for all slaves\n");
            pthread_mutex_unlock(&printf_lock);

            pthread_mutex_lock(&IOmap_lock);
            ec_slave[0].state = EC_STATE_INIT;
            /* request INIT state for all slaves */
            ec_writestate(0);
            pthread_mutex_unlock(&IOmap_lock);
        }
        else {
            pthread_mutex_unlock(&IOmap_lock);

            pthread_mutex_lock(&printf_lock);
            printf("No slaves found!\n");
            pthread_mutex_unlock(&printf_lock);
        }

        // stop SOEM, close socket
        pthread_mutex_lock(&printf_lock);
        printf("Closing SOEM socket...\n");
        pthread_mutex_unlock(&printf_lock);

        ec_close();
    }
    else {
        pthread_mutex_lock(&printf_lock);
        printf("No socket connection on %s\nPlease excecute as root!\n",ifname);
        pthread_mutex_unlock(&printf_lock);
    }

    if (pthread_mutex_destroy(&IOmap_lock) != 0) {
        perror("ERROR pthread_mutex_destroy has failed for IOmap_lock");
        exit(1);
    }
}


//Function to check that all slaves are alive, and reinitialize them if needed.
// Copied almost verbatim from SOEM/test/linux/simple_test/simple_test.c::ecatcheck()
OSAL_THREAD_FUNC ecat_check( void *ptr ) {
    (void)ptr; // Not used, reference it to quiet down the compiler

    while(1) {

        pthread_mutex_lock(&IOmap_lock);
        if( inOP && ((wkc < expectedWKC) || ec_group[currentgroup].docheckstate)) {

            /* one ore more slaves are not responding */
            updating = FALSE;
            ec_group[currentgroup].docheckstate = FALSE;
            ec_readstate();
            for (uint16 slave = 1; slave <= ec_slavecount; slave++) {
                if ((ec_slave[slave].group == currentgroup) && (ec_slave[slave].state != EC_STATE_OPERATIONAL)) {
                      ec_group[currentgroup].docheckstate = TRUE;
                    if (ec_slave[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
                        pthread_mutex_lock(&printf_lock);
                        printf("ERROR : slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
                        pthread_mutex_unlock(&printf_lock);
                        ec_slave[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
                        ec_writestate(slave);
                    }
                    else if(ec_slave[slave].state == EC_STATE_SAFE_OP) {
                        pthread_mutex_lock(&printf_lock);
                        printf("WARNING : slave %d is in SAFE_OP, change to OPERATIONAL.\n", slave);
                        pthread_mutex_unlock(&printf_lock);
                        ec_slave[slave].state = EC_STATE_OPERATIONAL;
                        ec_writestate(slave);
                    }
                      else if(ec_slave[slave].state > EC_STATE_NONE) {
                        if (ec_reconfig_slave(slave, EC_TIMEOUTMON)) {
                            ec_slave[slave].islost = FALSE;
                            pthread_mutex_lock(&printf_lock);
                            printf("MESSAGE : slave %d reconfigured\n",slave);
                            pthread_mutex_unlock(&printf_lock);
                        }
                    }
                    else if(!ec_slave[slave].islost) {
                        /* re-check state */
                        ec_statecheck(slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                        if (ec_slave[slave].state == EC_STATE_NONE) {
                            ec_slave[slave].islost = TRUE;
                            pthread_mutex_lock(&printf_lock);
                            printf("ERROR : slave %d lost\n",slave);
                            pthread_mutex_unlock(&printf_lock);
                        }
                    }
                }
                if (ec_slave[slave].islost) {
                      if(ec_slave[slave].state == EC_STATE_NONE) {
                            if (ec_recover_slave(slave, EC_TIMEOUTMON)) {
                                ec_slave[slave].islost = FALSE;
                                pthread_mutex_lock(&printf_lock);
                                printf("MESSAGE : slave %d recovered\n",slave);
                                pthread_mutex_unlock(&printf_lock);
                        }
                    }
                    else {
                        ec_slave[slave].islost = FALSE;
                        pthread_mutex_lock(&printf_lock);
                        printf("MESSAGE : slave %d found\n",slave);
                        pthread_mutex_unlock(&printf_lock);
                    }
                }
            }
            if(!ec_group[currentgroup].docheckstate) {
                pthread_mutex_lock(&printf_lock);
                printf("OK : all slaves resumed OPERATIONAL.\n");
                pthread_mutex_unlock(&printf_lock);
                updating = TRUE;
            }
        }
        pthread_mutex_unlock(&IOmap_lock);

        osal_usleep(PLC_waittime_checkAlive);
    }
}
