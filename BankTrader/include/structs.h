#include <semaphore.h>

#include "trader.h"
#include "exchange.h"
#include "protocol.h"

// Account struct and allAccounts array
typedef struct account {
    quantity_t quantity;         // Quantity bought/sold/traded/canceled
    quantity_t inventory;       // Inventory
    funds_t balance;            // Account balance
    char *username;             // Username used to login
    pthread_mutex_t mLock;      // Thread lock
} ACCOUNT;

ACCOUNT allAccounts[MAX_ACCOUNTS];

// Trader struct and allTraders array
typedef struct trader {
    int fileDesc;               // File descriptor
    int refCount;               // Number of references to the trader
    char *username;             // Username used to login
    ACCOUNT *currAccount;       // Account associated with trader
    pthread_mutexattr_t attr;   // Attribute to make mutex recursive
    pthread_mutex_t mLock;      // Thread lock
} TRADER;

TRADER allTraders[MAX_TRADERS];


// Order struct
typedef struct order {
    funds_t bid;                // Highest bid
    funds_t ask;                // Lowest ask price
    quantity_t quantity;        // Quantity bought in order
    orderid_t orderid;          // Id of the order
    struct order *nextOrder;    // Order following this order
    TRADER *trader;             // Trader associated with exchange
} ORDER;

// Exchange struct
typedef struct exchange {
    funds_t last;               // Last trade price
    funds_t highest_bid;        // Current highest bid price
    funds_t highest_ask;        // Current highest ask price
    int numExchgs;              // Number of exchanges
    int finished;               // Called SIGHUP
    ORDER *currOrder;           // Order associated with exchange
    sem_t madeXchg  ;           // Semaphore for when exchange is made
    sem_t waitForChange;        // Semaphore waiting for exchange
    pthread_mutexattr_t attr;   // Attribute to make mutex recursive
    pthread_mutex_t mLock;      // Thread lock (mutex)
} EXCHANGE;

pthread_t mtid;
pthread_mutex_t allTraLock;
pthread_mutex_t allAccLock;
void *matchmaking();