#include <string.h>

#include "account.h"
#include "structs.h"
#include "csapp.h"
#include "debug.h"

int accounts_init(void) {
    // Initializing each attribute in the account
    for(int i = 0; i < MAX_ACCOUNTS; i++) {
        allAccounts[i].quantity = 0;
        allAccounts[i].inventory = 0;
        allAccounts[i].balance = 0;
        allAccounts[i].username = NULL;
        pthread_mutex_init(&allAccounts[i].mLock, NULL);
    }

    // Initialize lock for account list
    pthread_mutex_init(&allAccLock, NULL);

    return EXIT_SUCCESS;
}

void accounts_fini(void) {
    // Set all attributes back to when it was originally initialized
    // Free the name since it was malloced if not NULL
    for(int i = 0; i < MAX_ACCOUNTS; i++) {
        allAccounts[i].quantity = 0;
        allAccounts[i].inventory = 0;
        allAccounts[i].balance = 0;
        if(allAccounts[i].username != NULL) Free(allAccounts[i].username);
        pthread_mutex_destroy(&allAccounts[i].mLock);
    }

    // Destroy lock for account list
    pthread_mutex_destroy(&allAccLock);
}

ACCOUNT *account_lookup(char *name) {
    pthread_mutex_lock(&allAccLock);
    // If username in account matches name, return account
    for(int i = 0; i < MAX_ACCOUNTS; i++) {
        
        // If username is NULL, then continue since strcmp would cause an error
        if(allAccounts[i].username == NULL) continue;

        // If account username matches name, return pointer to account
        if(!strcmp(allAccounts[i].username, name)) {
            pthread_mutex_unlock(&allAccLock);
            return &allAccounts[i];
        }
    }

    // Create new account for name
    for(int i = 0; i < MAX_ACCOUNTS; i++) {
        if(allAccounts[i].username == NULL) {
            // Set all necessary components
            allAccounts[i].quantity = 0;
            allAccounts[i].inventory = 0;
            allAccounts[i].balance = 0;
            
            // Malloc space for username
            int nameLength = strlen(name) + 1;
            allAccounts[i].username = Malloc(nameLength);
            memset(allAccounts[i].username, 0, nameLength);
            strcpy(allAccounts[i].username, name);

            // Return new account
            pthread_mutex_unlock(&allAccLock);
            return &allAccounts[i];
        } 
    }
    pthread_mutex_unlock(&allAccLock);
    return NULL;
}

void account_increase_balance(ACCOUNT *account, funds_t amount) {
    // Lock mutex
    pthread_mutex_lock(&allAccLock);
    pthread_mutex_lock(&account->mLock);
    // Increase by specified amount
    account->balance = account->balance + amount;

    // Unlock mutex
    pthread_mutex_unlock(&account->mLock);
    pthread_mutex_unlock(&allAccLock);
}

int account_decrease_balance(ACCOUNT *account, funds_t amount) {
    // Lock mutex
    pthread_mutex_lock(&allAccLock);
    pthread_mutex_lock(&account->mLock);

    // Check if amount
    if(account->balance < amount) {
        // Lock mutex
        pthread_mutex_unlock(&allAccLock);
        pthread_mutex_unlock(&account->mLock);
        return EXIT_FAILURE;
    }

    // Decrease by specified amount
    account->balance = account->balance - amount;

    // Unlock mutex
    pthread_mutex_unlock(&account->mLock);
    pthread_mutex_unlock(&allAccLock);
    return EXIT_SUCCESS;
}

void account_increase_inventory(ACCOUNT *account, quantity_t quantity) {
    // Lock mutex
    pthread_mutex_lock(&allAccLock);
    pthread_mutex_lock(&account->mLock);
    
    // Increase by specified amount
    account->inventory = account->inventory + quantity;

    // Unlock mutex
    pthread_mutex_unlock(&account->mLock);
    pthread_mutex_unlock(&allAccLock);
}

int account_decrease_inventory(ACCOUNT *account, quantity_t quantity) {
    // Lock mutex
    pthread_mutex_lock(&allAccLock);
    pthread_mutex_lock(&account->mLock);

    // Check if amount
    if(account->inventory < quantity) {
        // Unlock mutex
        pthread_mutex_unlock(&allAccLock);
        pthread_mutex_unlock(&account->mLock);
        return EXIT_FAILURE;
    }

    // Decrease by specified amount
    account->inventory = account->inventory - quantity;

    // Unlock mutex
    pthread_mutex_unlock(&account->mLock);
    pthread_mutex_unlock(&allAccLock);
    return EXIT_SUCCESS;
}

void account_get_status(ACCOUNT *account, BRS_STATUS_INFO *infop) {
    // Lock account and list
    pthread_mutex_lock(&account->mLock);

    // Get the balance, inventory, and quantity for infop
    infop->inventory = htonl(account->inventory);
    infop->balance = htonl(account->balance);

    // Unlock account and list
    pthread_mutex_unlock(&account->mLock);
}