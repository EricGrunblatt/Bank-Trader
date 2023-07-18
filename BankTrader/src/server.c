#include <string.h>

#include "server.h"
#include "csapp.h"
#include "debug.h"
#include "exchange.h"
#include "trader.h"
#include "protocol.h"
#include "structs.h"

void statusHelper(BRS_STATUS_INFO *statusP, TRADER *traderP, ACCOUNT *accountP, orderid_t id) {
    // Set the balance, inventory, and quantity for status
    account_get_status(accountP, statusP);

    // Create new info pointer for calling exchange_get_status()
    BRS_STATUS_INFO *infoP = Malloc(sizeof(BRS_STATUS_INFO));
    memset(infoP, 0, sizeof(BRS_STATUS_INFO));
    exchange_get_status(exchange, accountP, infoP);
    
    // Set infoP data to statusP
    if(infoP != NULL) {
        statusP->bid = infoP->bid;
        statusP->ask = infoP->ask;
        statusP->last = infoP->last;
        if(id < 0) statusP->orderid = infoP->orderid;
        else statusP->orderid = htonl(id);
    }

    // Free info pointer as it is no longer being used
    Free(infoP);
}

void cli_send_nack(int fileDesc) {
    // Create new packet, allocate space for it, set type to nack, and size to 0
    BRS_PACKET_HEADER *newPkt = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(newPkt, 0, sizeof(BRS_PACKET_HEADER));
    newPkt->type = BRS_NACK_PKT;
    newPkt->size = 0;
    
    // Send packet using trader file descriptor, newPkt variable, NULL since NACK has no payload
    proto_send_packet(fileDesc, newPkt, NULL);

    // Free newPkt as it is no longer being used, then return EXIT_SUCCESS
    Free(newPkt);
}

void *brs_client_service(void *arg) {
    // Parameter arg is a pointer to the integer file descriptor used to communicate with client
    // Once this file descriptor has been retrieved, the storage it occupied needs to be freed
    int fileDesc = *((int *)arg);
    Free(arg);

    // The thread must then become detached, so that it does not have to be explicitly reaped
    Pthread_detach(pthread_self());
    
    // It must register the client file descriptor with the client registry
    if(creg_register(client_registry, fileDesc) == EXIT_FAILURE) return NULL;

    // Set up packets, pointer to trader, and pointer to payload
    BRS_PACKET_HEADER *brsHeader = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(brsHeader, 0, sizeof(BRS_PACKET_HEADER));
    TRADER *newTrader = NULL;
    ACCOUNT *newAccount = NULL;
    void *payloadp = NULL;

    /* The thread should enter a service loop in which it repeatedly receives a request packet 
       sent by the client, carries out the request, and sends any response packets */
    while(proto_recv_packet(fileDesc, brsHeader, &payloadp) == 0) {
        int pktSize = brsHeader->size;
        
        if(brsHeader->type == BRS_LOGIN_PKT && newTrader != NULL) cli_send_nack(fileDesc);
        else if(brsHeader->type == BRS_LOGIN_PKT) {
            // Make the username the size of the packet + 1 for null terminator
            char username[pktSize+1];
            memcpy(&username[0], payloadp, pktSize);
            *(username + pktSize) = '\0';

            // Free payloadp since it was used in protocol and is no longer being used
            Free(payloadp);

            // Log the trader in, then create packet header to send trader info back
            newTrader = trader_login(fileDesc, username);
            if(newTrader == NULL) {
                cli_send_nack(fileDesc);
                continue;
            }
            newAccount = account_lookup(newTrader->username);

            BRS_PACKET_HEADER *pktForClient = Malloc(sizeof(BRS_PACKET_HEADER));
            memset(pktForClient, 0, sizeof(BRS_PACKET_HEADER));
            
            // Set type depending on whether newTrader received a NULL pointer or not
            // Set size of packe to 0 since there is no payload
            if(newTrader != NULL) pktForClient->type = BRS_ACK_PKT;
            else pktForClient->type = BRS_NACK_PKT;
            pktForClient->size = 0;

            // Send the packet, then free the header
            proto_send_packet(fileDesc, pktForClient, NULL);
            Free(pktForClient);
        }

        if(newTrader != NULL) {
            if(brsHeader->type == BRS_STATUS_PKT) {
                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, -1);
                trader_send_ack(newTrader, status);
                Free(status);

            } else if(brsHeader->type == BRS_DEPOSIT_PKT) {
                // Set deposit pointer to data from packet
                BRS_FUNDS_INFO *depositP = (BRS_FUNDS_INFO *)payloadp;
                account_increase_balance(newAccount, ntohl(depositP->amount));
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, -1);
                trader_send_ack(newTrader, status);
                Free(status);
            
            } else if(brsHeader->type == BRS_WITHDRAW_PKT) {
                // Set withdraw pointer to data from packet
                BRS_FUNDS_INFO *withdrawP = (BRS_FUNDS_INFO *)payloadp;
                if(account_decrease_balance(newAccount, ntohl(withdrawP->amount)) == EXIT_FAILURE) {
                    trader_send_nack(newTrader);
                }
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, -1);
                trader_send_ack(newTrader, status);
                Free(status);

            } else if(brsHeader->type == BRS_ESCROW_PKT) {
                // Set escrow pointer to data from packet
                BRS_ESCROW_INFO *escrowP = (BRS_ESCROW_INFO *)payloadp;
                account_increase_inventory(newAccount, ntohl(escrowP->quantity));
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, -1);
                trader_send_ack(newTrader, status);
                Free(status);

            } else if(brsHeader->type == BRS_RELEASE_PKT) {
                // Set release pointer to data from packet
                BRS_ESCROW_INFO *releaseP = (BRS_ESCROW_INFO *)payloadp;
                if(account_decrease_inventory(newAccount, ntohl(releaseP->quantity)) == EXIT_FAILURE) {
                    trader_send_nack(newTrader);
                }
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, -1);
                trader_send_ack(newTrader, status);
                Free(status);
                
            } else if(brsHeader->type == BRS_BUY_PKT) {
                // Set buy pointer to data from packet
                BRS_ORDER_INFO *buyP = (BRS_ORDER_INFO *)payloadp;
                quantity_t quant = ntohl(buyP->quantity);
                funds_t price = ntohl(buyP->price);
                orderid_t buyId = exchange_post_buy(exchange, newTrader, quant, price);
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, buyId);

                // If buyId > 0 (successful), send ACK packet
                if(buyId > 0) trader_send_ack(newTrader, status);
                else  trader_send_nack(newTrader);
                Free(status);

            } else if(brsHeader->type == BRS_SELL_PKT) {
                // Set sell pointer to data from packet
                BRS_ORDER_INFO *sellP = (BRS_ORDER_INFO *)payloadp;
                quantity_t quant = ntohl(sellP->quantity);
                funds_t price = ntohl(sellP->price);
                orderid_t sellId = exchange_post_sell(exchange, newTrader, quant, price);
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                statusHelper(status, newTrader, newAccount, sellId);

                // If sellId > 0 (successful), send ACK packet
                if(sellId > 0) trader_send_ack(newTrader, status);
                else  trader_send_nack(newTrader);
                Free(status);
                
            } else if(brsHeader->type == BRS_CANCEL_PKT) {
                // Set cancel pointer to data from packet
                BRS_CANCEL_INFO *cancelP = (BRS_CANCEL_INFO *)payloadp;
                orderid_t cancelId = ntohl(cancelP->order);
                quantity_t *quant = Malloc(sizeof(quantity_t));
                memset(quant, 0, sizeof(quantity_t));
                int isCanceled = exchange_cancel(exchange, newTrader, cancelId, quant);
                Free(payloadp);

                BRS_STATUS_INFO *status = Malloc(sizeof(BRS_STATUS_INFO));
                memset(status, 0, sizeof(BRS_STATUS_INFO));
                
                // Set the balance, inventory, and quantity for status
                status->quantity = htonl(*quant);
                status->inventory = htonl(newAccount->inventory);
                status->balance = htonl(newAccount->balance);
                status->orderid = htonl(cancelId);

                // Create new info pointer for calling exchange_get_status()
                BRS_STATUS_INFO *infoP = Malloc(sizeof(BRS_STATUS_INFO));
                memset(infoP, 0, sizeof(BRS_STATUS_INFO));
                exchange_get_status(exchange, newAccount, infoP);
                
                // Set infoP data to statusP
                if(infoP != NULL) {
                    status->bid = infoP->bid;
                    status->ask = infoP->ask;
                    status->last = infoP->last;
                }

                // Free info pointer as it is no longer being used
                Free(infoP);

                // If isCanceled > 0 (successful), send ACK packet
                if(isCanceled == EXIT_SUCCESS) trader_send_ack(newTrader, status);
                else  trader_send_nack(newTrader);

                Free(status);
            }
        } else cli_send_nack(fileDesc);        
    }

    // Free header as it is no longer being used and unregister clients
    if(newTrader != NULL) trader_logout(newTrader);
    creg_unregister(client_registry, fileDesc);
    free(brsHeader);
    return NULL;
}