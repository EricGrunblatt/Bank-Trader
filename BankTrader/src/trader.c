#include <string.h>

#include "trader.h"
#include "protocol.h"
#include "debug.h"
#include "account.h"
#include "structs.h"
#include "csapp.h"

int traders_init(void) {
    // Initializing all traders
    for(int i = 0; i < MAX_TRADERS; i++) {
        allTraders[i].fileDesc = -1;
        allTraders[i].refCount = 0;
        allTraders[i].username = NULL;
        allTraders[i].currAccount = NULL;
        pthread_mutexattr_init(&allTraders[i].attr);
        pthread_mutexattr_settype(&allTraders[i].attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&allTraders[i].mLock, NULL);
    }

    // Initialize trader list mutex
    pthread_mutex_init(&allTraLock, NULL);

    return EXIT_SUCCESS;
}

void traders_fini(void) {
    // Finalizing all traders
    for(int i = 0; i < MAX_TRADERS; i++) {
        allTraders[i].fileDesc = -1;
        allTraders[i].refCount = 0;
        if(allTraders[i].username != NULL) Free(allTraders[i].username);
        allTraders[i].currAccount = NULL;
        pthread_mutex_destroy(&allTraders[i].mLock);
    }

    // Destroy list lock
    pthread_mutex_destroy(&allTraLock);
}

TRADER *trader_login(int fd, char *name) {
    // Lock trader list
    pthread_mutex_lock(&allTraLock);

    // Find trader with same name and fd
    for(int i = 0; i < MAX_TRADERS; i++) {
        // If username is NULL, then continue since strcmp would cause an error
        if(allTraders[i].username == NULL) continue;

        // If username is not NULL and has the same file descriptor, return the trader
        if(!strcmp(allTraders[i].username, name) && allTraders[i].fileDesc == fd) {
            pthread_mutex_unlock(&allTraLock);
            return &allTraders[i];
        }
    }

    // No trader was found, so login and set first NULL spot to trader
    for(int i = 0; i < MAX_TRADERS; i++) {
        if(allTraders[i].username == NULL) {
            // Call account lookup, if it does not have it in system, it will be created
            account_lookup(name);

            // Set all necessary components for trader
            allTraders[i].fileDesc = fd;
            allTraders[i].refCount = 0;

            // Malloc space for username
            int nameLength = strlen(name) + 1;
            allTraders[i].username = Malloc(nameLength);
            memset(allTraders[i].username, 0, nameLength);
            strcpy(allTraders[i].username, name);

            // Set account attribute in trader to corresponding account
            allTraders[i].currAccount = trader_get_account(&allTraders[i]);
    
            // Mutex already initialized, so return trader
            pthread_mutex_unlock(&allTraLock);
            return &allTraders[i];
        }
    }
    pthread_mutex_unlock(&allTraLock);
    return NULL;
}

void trader_logout(TRADER *trader) {
    // Lock the trader mutex to change the file descriptor and name
    pthread_mutex_lock(&allTraLock);
    pthread_mutex_lock(&trader->mLock);

    // Remove the reference to the trader, change file descriptor to -1 and username to NULL
    trader_unref(trader, "logout");
    trader->fileDesc = -1;
    trader->username = NULL;
    trader->refCount = 0;

    // Unlock the trader mutex
    pthread_mutex_unlock(&trader->mLock);
    pthread_mutex_unlock(&allTraLock);
    Free(trader);
}

TRADER *trader_ref(TRADER *trader, char *why) {
    // Lock the trader mutex to change the count
    pthread_mutex_lock(&allTraLock);
    pthread_mutex_lock(&trader->mLock);

    // Change the refCount
    trader->refCount = trader->refCount + 1;

    // Unlock the trader mutex
    pthread_mutex_unlock(&trader->mLock);
    pthread_mutex_unlock(&allTraLock);

    return trader;
}

void trader_unref(TRADER *trader, char *why) {
    // Lock mutex, to change components
    pthread_mutex_lock(&allTraLock);
    pthread_mutex_lock(&trader->mLock);

    // Change count and file descriptor
    trader->refCount = trader->refCount - 1;

    // Unlock the trader mutex
    pthread_mutex_unlock(&trader->mLock);
    pthread_mutex_unlock(&allTraLock);
}

ACCOUNT *trader_get_account(TRADER *trader) {
    for(int i = 0; i < MAX_ACCOUNTS; i++) {
        if(!strcmp(trader->username, allAccounts[i].username)) return &allAccounts[i];
    }

    return NULL;
}

int trader_send_packet(TRADER *trader, BRS_PACKET_HEADER *pkt, void *data) {
    // Lock the trader mutex to get contents
    pthread_mutex_lock(&trader->mLock);

    // Send the packet
    proto_send_packet(trader->fileDesc, pkt, data);

    // Unlock the trader mutex
    pthread_mutex_unlock(&trader->mLock);

    return EXIT_SUCCESS;
}

int trader_broadcast_packet(BRS_PACKET_HEADER *pkt, void *data) {
    // Lock the list
    pthread_mutex_lock(&allTraLock);

    // Send packet to all traders with a non-negative file descriptor
    for(int i = 0; i < MAX_TRADERS; i++) {
        if(allTraders[i].fileDesc != -1 && allTraders[i].username != NULL) trader_send_packet(&allTraders[i], pkt, data);
    }

    // Unlock the list
    pthread_mutex_unlock(&allTraLock);

    return EXIT_SUCCESS;
}

int trader_send_ack(TRADER *trader, BRS_STATUS_INFO *info) {
    // Create new packet, allocate space for it, set type to ack, and size to info->size
    BRS_PACKET_HEADER *newPkt = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(newPkt, 0, sizeof(BRS_PACKET_HEADER));
    if(newPkt == NULL) return EXIT_FAILURE;

    // Lock the trader mutex to get contents
    pthread_mutex_lock(&allTraLock);
    pthread_mutex_lock(&trader->mLock);

    newPkt->type = BRS_ACK_PKT;
    newPkt->size = sizeof(BRS_ACK_PKT);
    
    // Send packet using trader file descriptor, newPkt variable, and info (could be NULL or not)
    proto_send_packet(trader->fileDesc, newPkt, info);

    // Free newPkt as it is no longer being used, then return EXIT_SUCCESS
    Free(newPkt);

    // Unlock the trader mutex to get contents
    pthread_mutex_unlock(&trader->mLock);
    pthread_mutex_unlock(&allTraLock);
    return EXIT_SUCCESS;
}

int trader_send_nack(TRADER *trader) {
    // Create new packet, allocate space for it, set type to nack, and size to 0
    BRS_PACKET_HEADER *newPkt = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(newPkt, 0, sizeof(BRS_PACKET_HEADER));
    if(newPkt == NULL) return EXIT_FAILURE;

    // Lock the trader mutex to get contents
    pthread_mutex_lock(&allTraLock);
    pthread_mutex_lock(&trader->mLock);
    
    newPkt->type = BRS_NACK_PKT;
    newPkt->size = 0;
    
    // Send packet using trader file descriptor, newPkt variable, NULL since NACK has no payload
    proto_send_packet(trader->fileDesc, newPkt, NULL);

    // Free newPkt as it is no longer being used, then return EXIT_SUCCESS
    Free(newPkt);

    // Unlock the trader mutex to get contents
    pthread_mutex_unlock(&trader->mLock);
    pthread_mutex_unlock(&allTraLock);
    return EXIT_SUCCESS;
}