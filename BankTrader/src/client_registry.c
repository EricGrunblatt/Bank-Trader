#include <semaphore.h>
#include <pthread.h>

#include "server.h"
#include "csapp.h"
#include "client_registry.h"
#include "debug.h"
#include "structs.h"

/*** HELPFUL TIPS ***/
// Use a mutex to protect access to the thread counter data.  
// Use a semaphore to perform the required blocking in the creg_wait_for_empty()
// Use P(sem_t *s) for sem_wait, used in creg_wait_for_empty()
// Use V(sem_t *s) for sem_post, used in creg_wait_for_empty() and creg_unregister()
// Destroy semaphores and mutexes before deallocated

typedef struct client_registry {
    int numClients; // Number of clients registered (used to track if more than 0 clients are registered)
    int fdBuf[FD_SETSIZE]; // File descriptors for clients
    sem_t semGuard; // Semaphore for clients (acts as guard)
    pthread_mutex_t mLock; // Mutex for clients (acts as a lock)
} CLIENT_REGISTRY;

CLIENT_REGISTRY *creg_init() {
    // Create the newClient variable
    CLIENT_REGISTRY *newClient = Malloc(sizeof(CLIENT_REGISTRY));
    memset(newClient, 0, sizeof(CLIENT_REGISTRY));
    if(newClient == NULL) return NULL;

    // Set the count (numClients) to 0
    newClient->numClients = 0;

    // Set each file descriptor to -1 since none have been assigned yet
    for(int i = 0; i < FD_SETSIZE; i++) {
        newClient->fdBuf[i] = -1;
    }

    // Initialize semaphore and mutex
    int s = sem_init(&newClient->semGuard, 0, 0);
    int m = pthread_mutex_init(&newClient->mLock, NULL);

    // Return null if either of the initializations fail
    if(s < 0 || m < 0) {
        free(newClient);
        return NULL;
    }

    // Return client
    return newClient;
}

void creg_fini(CLIENT_REGISTRY *cr) {
    // Destroy semaphore and mutex before it is deallocated
    sem_destroy(&cr->semGuard);
    pthread_mutex_destroy(&cr->mLock);

    // Deallocate client pointer
    Free(cr);
}

int creg_register(CLIENT_REGISTRY *cr, int fd) {
    // If file descriptor is not -1, it is already registered, hence return EXIT_FAILURE
    if(cr->fdBuf[fd] != -1) return EXIT_FAILURE;

    // Lock the mutex since it is being used to set data for the client
    pthread_mutex_lock(&cr->mLock);

    // Increase the number of clients
    cr->numClients = cr->numClients + 1;

    // Set the file descriptor to the parameter given
    cr->fdBuf[fd] = fd;

    // Unlock the mutex as the client is no longer being used
    pthread_mutex_unlock(&cr->mLock);

    return EXIT_SUCCESS;
}

int creg_unregister(CLIENT_REGISTRY *cr, int fd) {
    // If file descriptor is not fd, it is already unregistered, hence return EXIT_FAILURE
    if(cr->fdBuf[fd] != fd) return EXIT_FAILURE;

    // Lock the mutex since it is being used to remove data from the client
    pthread_mutex_lock(&cr->mLock);

    // Decrease the number of clients
    cr->numClients = cr->numClients - 1;

    // Set the file descriptor to the parameter given
    close(cr->fdBuf[fd]);
    cr->fdBuf[fd] = -1;

    // Check if numClients == 0
    if(cr->numClients == 0) {
        if(sem_post(&cr->semGuard) < 0) return EXIT_FAILURE;
    }

    // Unlock the mutex as the client is no longer being used
    pthread_mutex_unlock(&cr->mLock);

    return EXIT_SUCCESS;
}

void creg_wait_for_empty(CLIENT_REGISTRY *cr) {
    // Wait until the number of registered clients reaches 0
    // Will throw an error if semGuard < 0
    P(&cr->semGuard);
}

void creg_shutdown_all(CLIENT_REGISTRY *cr) {
    // Loop through each file descriptor to unregister and shutdown each one
    for(int i = 0; i < FD_SETSIZE; i++) {
        if(cr->fdBuf[i] != -1) {
            close(cr->fdBuf[i]);
            shutdown(cr->fdBuf[i], SHUT_RD);
            creg_unregister(cr, cr->fdBuf[i]);
            cr->fdBuf[i] = -1;
        }
    }
}