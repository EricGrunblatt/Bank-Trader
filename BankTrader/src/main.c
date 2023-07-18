#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>

#include "client_registry.h"
#include "exchange.h"
#include "account.h"
#include "trader.h"
#include "debug.h"
#include "server.h"
#include "csapp.h"

extern EXCHANGE *exchange;
extern CLIENT_REGISTRY *client_registry;

/*
 * Function called to cleanly shut down the server.
 */
static void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);
    
    debug("Waiting for service threads to terminate...");
    creg_wait_for_empty(client_registry);
    debug("All service threads terminated.");

    // Finalize modules.
    creg_fini(client_registry);
    exchange_fini(exchange);
    traders_fini();
    accounts_fini();

    debug("Bourse server terminating");
    exit(status);
}

void sighup_handler(int sig) {
    if(sig == SIGHUP) terminate(EXIT_SUCCESS);
}

/*
 * "Bourse" exchange server.
 *
 * Usage: bourse <port>
 */
int main(int argc, char* argv[]){
    // Make sure argc > 1
    if(argc <= 1) exit(EXIT_SUCCESS);

    /* Option processing should be performed here. Option '-p <port>' is required 
    in order to specify the port number on which the server should listen. */
    int option;
    char *port;
    while((option = getopt(argc, argv, "p:")) != EOF) {
        switch(option) {
            case 'p':
                port = optarg++;
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

    // Set up sighup handler (RESORT BACK IF ANYTHING WEIRD HAPPENS)
    // struct sigaction optns;
    // optns.sa_handler = sighup_handler;
    // memset(&optns, 0, sizeof(optns));
    // if(sigaction(SIGHUP, &optns, NULL) == -1) exit(EXIT_FAILURE);
    
    // Set up sighup handler (Signal() function uses sigaction)
    Signal(SIGHUP, sighup_handler);

    // Perform required initializations of the client_registry,
    // maze, and player modules.
    client_registry = creg_init();
    accounts_init();
    traders_init();
    exchange = exchange_init();

    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen=sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        memset(connfdp, 0, sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, brs_client_service, connfdp);
    }

    terminate(EXIT_FAILURE);
}
