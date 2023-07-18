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

void createNotifyPacket(BRS_NOTIFY_INFO *notify, quantity_t q, funds_t p, orderid_t b, orderid_t s) {
    notify->quantity = htonl(q);
    notify->price = htonl(p);
    notify->buyer = htonl(b);
    notify->seller = htonl(s);
}

void broadcastAllPackets(BRS_NOTIFY_INFO *notify, TRADER *buyer, TRADER *seller) {
    // Send bought packet
    BRS_PACKET_HEADER *buy = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(buy, 0, sizeof(BRS_PACKET_HEADER));
    buy->type = BRS_BOUGHT_PKT;
    buy->size = sizeof(BRS_NOTIFY_INFO);
    trader_send_packet(buyer, buy, notify);
    Free(buy);
    
    // Send sold packet
    BRS_PACKET_HEADER *sell = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(sell, 0, sizeof(BRS_PACKET_HEADER));
    sell->type = BRS_SOLD_PKT;
    sell->size = sizeof(BRS_NOTIFY_INFO);
    trader_send_packet(seller, sell, notify);
    Free(sell);
    
    // Broadcast traded packet
    BRS_PACKET_HEADER *trade = Malloc(sizeof(BRS_PACKET_HEADER));
    memset(trade, 0, sizeof(BRS_PACKET_HEADER));
    trade->type = BRS_TRADED_PKT;
    trade->size = sizeof(BRS_NOTIFY_INFO);
    trader_broadcast_packet(trade, notify);
    Free(trade);
}

void removeOrder(EXCHANGE *exchange, ORDER *order) {
    ORDER *tempOrder = exchange->currOrder;
    while(tempOrder != NULL) {
        if(tempOrder == exchange->currOrder && tempOrder == order) {
            exchange->currOrder = order->nextOrder;
            break;
        }
        if(tempOrder->nextOrder == order) {
            tempOrder->nextOrder = order->nextOrder;
            break;
        }
        tempOrder = tempOrder->nextOrder;
    }
}


// Main matchmaking method
void *matchmaking(void *arg) {
    // Make the exchange variable from arg
    EXCHANGE *exchange = (EXCHANGE *)arg;

    while(1) {
        // Waiting until a buyer, sellers, or exchange finalize is posted
        P(&exchange->madeXchg);
        pthread_mutex_lock(&exchange->mLock);
        
        // Initialize buyer and seller
        ORDER *seller = exchange->currOrder;
        ORDER *buyer = exchange->currOrder;

        // Check if exchange is finalized, or there is a bidder/seller
        if(exchange->finished) break;
        else if(exchange->highest_bid == 0 || exchange->highest_ask == 0) debug("No one looking to buy or sell");
        else {
            // Run nested for loop to find a seller that may match a buyer's wants
            while(seller != NULL) {
                if(seller->ask <= 0) {
                    seller = seller->nextOrder;
                    continue;
                }

                // Go through each buyer until a bid is greater than or equal to the ask
                while(buyer != NULL) {
                    if(buyer->bid <= 0 || buyer->trader == seller->trader)  {
                        buyer = buyer->nextOrder;
                        continue;
                    }
                    if(buyer->bid >= seller->ask && buyer->bid > 0) {
                        // Get accounts for both traders
                        ACCOUNT *buyerAcc = trader_get_account(buyer->trader);
                        ACCOUNT *sellerAcc = trader_get_account(seller->trader);

                        int price = 0;
                        int quantity = 0;
                        price = (seller->ask > exchange->last) ? seller->ask : exchange->last;
                        price = (buyer->bid < price) ? buyer->bid : price;
                        // Decrease quantity and increase inventory for buyer, increase balance for seller 
                        if(buyer->quantity > seller->quantity) {
                            quantity = seller->quantity;
                            seller->quantity = 0;
                            buyer->quantity = buyer->quantity - seller->quantity;
                        } else if(buyer->quantity == seller->quantity) {
                            quantity = seller->quantity;
                            seller->quantity = 0;
                            buyer->quantity = 0;
                        } else {
                            quantity = buyer->quantity;
                            seller->quantity = seller->quantity - buyer->quantity;
                            buyer->quantity = 0;
                        }
                        account_increase_inventory(buyerAcc, quantity);
                        account_increase_balance(sellerAcc, price * quantity);
                        if(price < buyer->bid) account_increase_balance(buyerAcc, quantity * (buyer->bid - price));
                        exchange->last = price;

                        // Send packets by calling the functions
                        BRS_NOTIFY_INFO *notify = Malloc(sizeof(BRS_NOTIFY_INFO));
                        memset(notify, 0, sizeof(BRS_NOTIFY_INFO));
                        createNotifyPacket(notify, quantity, price, buyer->orderid, seller->orderid);
                        broadcastAllPackets(notify, buyer->trader, seller->trader);
                        Free(notify);

                        // Remove seller order from list
                        ORDER *tempS = seller;
                        ORDER *tempB = buyer;
                        if(tempB->quantity == 0) {
                            trader_unref(tempB->trader, "Seller sold inventory");
                            removeOrder(exchange, tempB);
                            buyer = exchange->currOrder;
                            Free(tempB);
                        }
                        if(tempS->quantity == 0) {
                            trader_unref(tempS->trader, "Buyer bought inventory");
                            removeOrder(exchange, tempS);
                            seller = exchange->currOrder;
                            Free(tempS);
                        }
                        
                    }
                    if(buyer != NULL) buyer = buyer->nextOrder;
                }
                if(seller != NULL) seller = seller->nextOrder;
            }
        }
        pthread_mutex_unlock(&exchange->mLock);
    }

    V(&exchange->waitForChange);
    return NULL;
}