#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <time.h>

#include "exchange.h"
#include "trader.h"
#include "protocol.h"
#include "debug.h"
#include "account.h"
#include "structs.h"
#include "csapp.h"

EXCHANGE *exchange_init() {
    // Create new exchange
    EXCHANGE *newExchange = Malloc(sizeof(EXCHANGE));
    memset(newExchange, 0, sizeof(EXCHANGE));
    if(newExchange == NULL) return NULL;

    // Set all integer components to 0
    newExchange->last = 0;
    newExchange->highest_bid = 0;
    newExchange->highest_ask = 0;
    newExchange->numExchgs = 0;
    newExchange->finished = 0;

    // Instantiate order
    newExchange->currOrder = NULL;

    // Initialize semaphores, mutex, and create thread
    sem_init(&newExchange->madeXchg, 0, 0);
    sem_init(&newExchange->waitForChange, 0, 0);
    pthread_mutexattr_init(&newExchange->attr);
    pthread_mutexattr_settype(&newExchange->attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&newExchange->mLock, NULL);
    Pthread_create(&mtid, NULL, matchmaking, newExchange);

    return newExchange;
}

void exchange_fini(EXCHANGE *xchg) {
    // Set all integer attributes to 0
    xchg->last = 0;
    xchg->highest_bid = 0;
    xchg->highest_ask = 0;
    xchg->numExchgs = 0;
    xchg->finished = 1;

    // Free malloced variables and destroy pthreads/mutexes
    if(xchg->currOrder != NULL) Free(xchg->currOrder);
    V(&xchg->madeXchg);
    P(&xchg->waitForChange);
    Pthread_join(mtid, NULL);
    Free(xchg);
}

void exchange_get_status(EXCHANGE *xchg, ACCOUNT *account, BRS_STATUS_INFO *infop) {
    // Set components of infop to components from xchg
    infop->last = htonl(xchg->last);
    infop->bid = htonl(xchg->highest_bid);
    infop->ask = htonl(xchg->highest_ask);
}

void exchange_sell_buy(EXCHANGE *xchg, TRADER *trader, ORDER *newOrder, quantity_t quantity, funds_t price, int isBuyer) {
    // Fill in necessary info
    if(isBuyer) {
        newOrder->bid = price;
        newOrder->ask = 0;
    } else {
        newOrder->bid = 0;
        newOrder->ask = price;
    }
    newOrder->quantity = quantity;
    newOrder->orderid = xchg->numExchgs;
    newOrder->trader = trader;
    newOrder->nextOrder = xchg->currOrder;
    xchg->currOrder = newOrder;

    ORDER *tempOrder = xchg->currOrder;
    int count = 0;
    while(tempOrder != NULL) {
        count++;
        tempOrder = tempOrder->nextOrder;
    }    
}

void exchange_post(ORDER *newOrder, quantity_t quantity, funds_t price, int isBuyer, int forCancel) {
    // Create new packet
    BRS_PACKET_HEADER *newPkt = Malloc(sizeof(BRS_PACKET_HEADER));
    BRS_NOTIFY_INFO *notifyType = Malloc(sizeof(BRS_NOTIFY_INFO));
    memset(newPkt, 0, sizeof(BRS_PACKET_HEADER));
    memset(notifyType, 0, sizeof(BRS_NOTIFY_INFO));
    // Fill in notifyType info first
    if(isBuyer) {
        notifyType->buyer = htonl(newOrder->orderid);
        notifyType->seller = htonl(0);
    } else {
        notifyType->buyer = htonl(0);
        notifyType->seller = htonl(newOrder->orderid);
    }
    
    notifyType->quantity = htonl(quantity);
    notifyType->price = htonl(price);
    // Fill in newPkt info, time is done in proto_send_packet
    if(forCancel) newPkt->type = BRS_CANCEL_PKT;
    else newPkt->type = BRS_POSTED_PKT;
    newPkt->size = sizeof(BRS_NOTIFY_INFO);

    // Broadcast packet to all traders
    trader_broadcast_packet(newPkt, notifyType);

    // Free variables, then, return orderid
    free(newPkt);
    free(notifyType);
}

orderid_t exchange_post_buy(EXCHANGE *xchg, TRADER *trader, quantity_t quantity, funds_t price) {
    // Lock the mutex for trader, then retrieve account
    pthread_mutex_lock(&xchg->mLock);
    ACCOUNT *currAccount = trader_get_account(trader);

    // Check if the trader's balance is enough to purchase
    if(account_decrease_balance(currAccount, quantity * price) == EXIT_SUCCESS) {
        // Increase the numExchgs count and trader count, change highest bid if price is greater
        xchg->numExchgs = xchg->numExchgs + 1;
        trader_ref(trader, "Placing Order");
        if(price > xchg->highest_bid) xchg->highest_bid = price;

        // Create new order struct
        ORDER *newOrder = Malloc(sizeof(ORDER));
        memset(newOrder, 0, sizeof(ORDER));
        exchange_sell_buy(xchg, trader, newOrder, quantity, price, 1);
        exchange_post(newOrder, quantity, price, 1, 0);

        // Post semaphore, unlock mutex and return
        V(&xchg->madeXchg);
        pthread_mutex_unlock(&xchg->mLock);
        return newOrder->orderid;
    }

    // Send nack packet, then unlock the mutex for trader
    trader_send_nack(trader);
    V(&xchg->waitForChange);
    pthread_mutex_unlock(&xchg->mLock);
    return EXIT_SUCCESS;
}

orderid_t exchange_post_sell(EXCHANGE *xchg, TRADER *trader, quantity_t quantity, funds_t price) {
    // Lock the mutex for trader, then retrieve account
    pthread_mutex_lock(&xchg->mLock);
    ACCOUNT *currAccount = trader_get_account(trader);

    // Check if trader's inventory contains enough for the quantity
    if(account_decrease_inventory(currAccount, quantity) == EXIT_SUCCESS) {
        // Increase the numExchgs count and trader count, change highest ask if price is greater
        xchg->numExchgs = xchg->numExchgs + 1;
        trader_ref(trader, "Making Sale");
        if(price > xchg->highest_ask) xchg->highest_ask = price;

        // Create new order struct
        ORDER *newOrder = Malloc(sizeof(ORDER));
        memset(newOrder, 0, sizeof(ORDER));
        exchange_sell_buy(xchg, trader, newOrder, quantity, price, 0);
        exchange_post(newOrder, quantity, price, 0, 0);

        // Post semaphore, unlock mutex and return
        V(&xchg->madeXchg);
        pthread_mutex_unlock(&xchg->mLock);
        return newOrder->orderid;
    }

    // Send nack packet, post semaphore, then unlock the mutex for trader
    trader_send_nack(trader);
    V(&xchg->waitForChange);
    pthread_mutex_unlock(&xchg->mLock);
    return EXIT_SUCCESS;
}

int exchange_cancel(EXCHANGE *xchg, TRADER *trader, orderid_t order, quantity_t *quantity) {
    // Lock the mutex for trader, then retrieve account
    pthread_mutex_lock(&xchg->mLock);
    ACCOUNT *currAccount = trader_get_account(trader);
    ORDER *currOrder = xchg->currOrder;
    ORDER *prevOrder = currOrder;

    // Search through all of the orders until the same orderid is found
    while(currOrder != NULL) {
        // Find what order matches orderid
        if(currOrder->orderid == order) {
            // Change the linked list so 
            if(currOrder == prevOrder) xchg->currOrder = NULL;
            else if(currOrder->nextOrder == NULL) prevOrder->nextOrder = NULL;
            else prevOrder->nextOrder = currOrder->nextOrder;

            // Set quantity pointer argument
            *quantity = currOrder->quantity;

            // Check if bid > 0 (the trader is a buyer), otherwise check if ask > 0 (the trader is a seller)
            if(currOrder->bid > 0) {
                // Increase account's balance
                account_increase_balance(currAccount, (currOrder->bid * currOrder->quantity));

                // Find the next highest bid
                ORDER *tempOrder = xchg->currOrder;
                xchg->highest_bid = 0;
                while(tempOrder != NULL) {
                    if(tempOrder->bid > xchg->highest_bid) xchg->highest_bid = tempOrder->bid;
                    tempOrder = tempOrder->nextOrder;
                }

                // Send broadcast packet
                exchange_post(currOrder, currOrder->quantity, currOrder->bid, 1, 1);
            } else if(currOrder->ask > 0) {
                // Increase account's inventory
                account_increase_inventory(currAccount, *quantity);

                // Find the next highest ask
                ORDER *tempOrder = xchg->currOrder;
                xchg->highest_ask = 0;
                while(tempOrder != NULL) {
                    if(tempOrder->ask > xchg->highest_ask) xchg->highest_ask = tempOrder->ask;
                    tempOrder = tempOrder->nextOrder;
                }

                // Send broadcast packet
                exchange_post(currOrder, *quantity, currOrder->bid, 0, 1);
            }

            // Decrement
            xchg->numExchgs = xchg->numExchgs - 1;
            trader_unref(trader, "Canceled Sale");
        
            // Free the currOrder, unlock the mutex and return
            Free(currOrder);
            pthread_mutex_unlock(&xchg->mLock);
            return EXIT_SUCCESS;
        }
        prevOrder = currOrder;
        currOrder = currOrder->nextOrder;
    }

    // Send nack packet, then unlock the mutex for trader
    trader_send_nack(trader);
    pthread_mutex_unlock(&xchg->mLock);
    return EXIT_FAILURE;
}

