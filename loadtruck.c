#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define DebugUsage
//#define DebugHashTable
//#define DebugProfile

//#define DebugInput
//#define DebugQueue
//#define DebugIngredients
//#define DebugDelivery

#define allocate(_amount) malloc(_amount)

#if 1 //emit output
    #define emitS(_S) puts(_S)
    #define emitF(_F,...) printf( _F , ##__VA_ARGS__ )
#else
    #define emitS(_S) 
    #define emitF(_F,...) 
#endif

#define MAX_LINE_LEN    32768
#define HASH_TABLE_SIZE 65536
#define MAX_BATCHES     512

#define add_recipe    (*(uint32_t*)"aggiungi_ricetta ")
#define remove_recipe (*(uint32_t*)"rimuovi_ricetta ")
#define replenishment (*(uint32_t*)"rifornimento ")
#define place_order   (*(uint32_t*)"ordine ")

#if 0 //debug info
  #define Debug(_fmt,...) fprintf( stderr, _fmt "\n", ##__VA_ARGS__ )
  #define Debug0(_fmt,...) fprintf( stderr, _fmt " " , ##__VA_ARGS__ )  
#else
  #define Debug(_fmt,...)
  #define Debug0(_fmt,...)  
#endif

#define DebugError(_fmt,...) fprintf( stderr, "ERROR: " _fmt "\n", ##__VA_ARGS__ );exit(1);
#define DebugWarn(_fmt,...) fprintf( stderr, _fmt "\n", ##__VA_ARGS__ );

int32_t iCurrentTime   =0; //current simulation time (in lines)
int32_t iNextTruckTime =0; //next time the truck arrives
int32_t iTruckPeriod   =0; //how often the truck to load arrives
int32_t iTruckCapacity =0; //how much the truck can load

#ifdef DebugProfile
  int g_iInsertions = 0 , g_iInsertionsChecks = 0 , g_iBatches=0 , g_iIgnoredBatches=0 , g_iBatchMergings = 0;
  #define _Profile(_Var) _Var++
#else  
  #define _Profile(_Var)
#endif



int32_t TextToken( char* pzText , char** pzStart ) {
  char* pz = pzText;  
  while ( *pz <= ' ' ) { pz++; }  //ignore leading spaces
  *pzStart = pz;  
  while ( *pz > ' ' ) { pz++; }  //read text
  return (int32_t)(pz - *pzStart);
}
int32_t NumToken( char* pzText , int32_t* piNumber ) {
  char* pz = pzText;
  int32_t iResult = 0;
  while ( *pz <= ' ' ) { pz++; }  //ignore leading spaces
  while ( *pz >= '0' && *pz <= '9' ) { iResult = iResult * 10 + (*pz - '0'); pz++; }  //read number
  *piNumber = iResult;
  return (int32_t)(pz - pzText);
  
}

typedef struct IngredientBatch {  
  int32_t          iGrams;
  int32_t          iExpiration;
} IngredientBatch;

typedef struct Ingredient Ingredient;
typedef struct Ingredient {
  Ingredient*      pNext;  
  char*            pzName;  
  int32_t          iGrams;
  int32_t          iNameLength;  
  int16_t          iFirstBatch;
  int16_t          iLastBatch;
  IngredientBatch  tBatch[MAX_BATCHES];  
  //buffer for name
} Ingredient;

typedef struct RecipeIngredient {
  Ingredient* pIngredient;  
  int32_t     iGrams;
} RecipeIngredient;

//forward reference to Recipe struct
typedef struct Recipe Recipe;
typedef struct Recipe {
  Recipe *pNext; //offset to next recipe
  int32_t           iIngredientCount;  
  int32_t           iNameLength;
  int32_t           iUsedCount; //how many times this recipe was used
  //buffer for name and ingredients
} Recipe;

typedef struct OrderQueue OrderQueue;
typedef struct OrderQueue {
  OrderQueue* pNext;
  Recipe* pRecipe;
  int32_t iTotalGrams;
  int32_t iQuantity;
  int32_t iOrderTime;
} OrderQueue;

typedef OrderQueue ToDeliverQueue; //same struct

Recipe*     g_HashTableRecipes[HASH_TABLE_SIZE] = {0};
Ingredient* g_HashTableIngredients[HASH_TABLE_SIZE] = {0};

uint32_t HashRecipeName(const char* pzStr, int iStrLen );
#define HashIngredientName HashRecipeName
Ingredient* LocateIngredient( uint32_t uHash , char* pzName , int32_t iNameLen ) {
  Ingredient* pResult = g_HashTableIngredients[uHash];
  while (pResult != 0) {
    if ( (pResult->iNameLength == iNameLen) && (memcmp( pResult->pzName , pzName , iNameLen ) == 0) ) {
      return pResult;
    }
    pResult = pResult->pNext;
  }
  return 0; //not found
}
Ingredient* CreateIngredient( uint32_t uHash , char* pzName , int32_t iNameLen ) {
  uint32_t uAllocSize = sizeof(Ingredient)+iNameLen+1;
  Ingredient* pResult = allocate( uAllocSize );  
  pResult->pNext = g_HashTableIngredients[uHash];
  pResult->pzName = (char*)(pResult+1);
  pResult->iNameLength = iNameLen;
  memcpy( pResult->pzName , pzName , iNameLen );
  pResult->pzName[iNameLen] = 0;
  pResult->iGrams = 0;  
  pResult->iFirstBatch = 0;
  pResult->iLastBatch = 0;
  g_HashTableIngredients[uHash] = pResult;
  return pResult;
}  
void        ReplenishIngredient( char* pzName , int32_t iNameLen , int32_t iGrams , int32_t iExpiration ) {
  _Profile(g_iBatches);
  if (iExpiration <= iCurrentTime) { _Profile(g_iIgnoredBatches) ; return; }    
  if (!iGrams) { _Profile(g_iIgnoredBatches); return; }  
    
  uint32_t uHash = HashIngredientName( pzName , iNameLen );
  Ingredient* pIngredient = LocateIngredient( uHash , pzName , iNameLen );
  if (pIngredient == 0) {
    pIngredient = CreateIngredient( uHash , pzName , iNameLen );
  }
  if ( (pIngredient->iFirstBatch == pIngredient->iLastBatch) && (pIngredient->iGrams) ) {
    DebugError("Too many batches for ingredient %s", pzName);    
  }

  //insert the new batch in the batches ring buffer (circular array)

  pIngredient->iGrams += iGrams;  
  IngredientBatch* pBatch;
  if (pIngredient->iGrams == iGrams) {
    //if there's no igredients, then add the first batch
    pBatch = pIngredient->tBatch+pIngredient->iLastBatch;    
  } else {
    int32_t iPrevBatch , iCurBatch=pIngredient->iLastBatch; 
    while (1) {      
      iPrevBatch = iCurBatch;
      iCurBatch = (iCurBatch-1) & (MAX_BATCHES-1);
      pBatch = pIngredient->tBatch+iCurBatch;
      //check if should insert here... or if we must first at the first batch
      //could do a binary search here, but seems overkill
      if ( (iPrevBatch==pIngredient->iFirstBatch) || (iExpiration > pBatch->iExpiration)) {
        //move all items to have room to insert the new batch
        int32_t iInsertBatch = pIngredient->iLastBatch;        
        while (1) {
          iPrevBatch = (iInsertBatch-1) & (MAX_BATCHES-1);
          if (iPrevBatch == iCurBatch) { break; } 
          pIngredient->tBatch[iInsertBatch] = pIngredient->tBatch[iPrevBatch];
          iInsertBatch = iPrevBatch;
        }    
        //set the pointer to the new batch
        pBatch = pIngredient->tBatch+iInsertBatch;
        break;
      } else if (pBatch->iExpiration == iExpiration) {
        //if expiration is the same, then merge the batches
        _Profile(g_iBatchMergings);
        pBatch->iGrams += iGrams;
        return;    
      }
    }
  }
  //set the new batch      
  pBatch->iGrams = iGrams;
  pBatch->iExpiration = iExpiration;
  pIngredient->iLastBatch = (pIngredient->iLastBatch+1) & (MAX_BATCHES-1);
  
}

Recipe*   LocateRecipe( uint32_t uHash , char* pzToken , int32_t iNameLen ) {
  Recipe* pResult = g_HashTableRecipes[uHash];
  while (pResult != 0) { //pResult+1 = pResult->pzName
    if ( (pResult->iNameLength == iNameLen) && (memcmp( pResult+1 , pzToken , iNameLen ) == 0) ) {
      return pResult;
    }
    pResult = pResult->pNext;
  }
  return 0; //not found
}
Recipe*   StartCreateRecipe( uint32_t uHash , char* pzToken , int32_t iNameLen , int32_t iMaxIngredients ) { 
  Recipe* pResult = allocate( sizeof(Recipe)+iNameLen+1+(sizeof(RecipeIngredient)*iMaxIngredients) ); 
  //TODO: check for malloc failure
  pResult->pNext = g_HashTableRecipes[uHash];
  char* pzName = (char*)(pResult+1);
  //pResult->pzName = (char*)(pResult+1);
  //pResult->pIngredients = (RecipeIngredient*)(pResult->pzName + iNameLen + 1);
  pResult->iNameLength = iNameLen;  
  memcpy( pzName , pzToken , iNameLen );
  pzName[iNameLen] = 0;
  pResult->iIngredientCount = 0;
  pResult->iUsedCount = 0;
  g_HashTableRecipes[uHash] = pResult;
  return pResult;
}
int32_t   AddRecipeIngredient( Recipe* pRecipe , char* pzIngredient , int32_t iNameLen , int32_t iGrams ) {
  RecipeIngredient* pRecipeIngredientList = (RecipeIngredient*)(((char*)(pRecipe+1))+pRecipe->iNameLength+1);
  RecipeIngredient* pRecipeIngredient = pRecipeIngredientList + pRecipe->iIngredientCount++;
  uint32_t uHash = HashIngredientName( pzIngredient , iNameLen );
  Ingredient* pIngredient = LocateIngredient( uHash , pzIngredient , iNameLen );
  if (pIngredient == 0) {
    pIngredient = CreateIngredient( uHash , pzIngredient , iNameLen );
  }
  pRecipeIngredient->pIngredient = pIngredient;
  pRecipeIngredient->iGrams      = iGrams;
  return pRecipe->iIngredientCount;
}
Recipe*   FinishCreateRecipe( Recipe* pRecipe , uint32_t uHash , int32_t iMaxIngredients ) {
  void* pRecipeOrg = pRecipe;
  uint32_t uAllocSize = sizeof(Recipe)+pRecipe->iNameLength+1+(sizeof(RecipeIngredient)*pRecipe->iIngredientCount);
  if ( iMaxIngredients == pRecipe->iIngredientCount ) {     
    return pRecipe; 
  } //no need to realloc
  DebugWarn("Ingredients count mismatch %i != %i", iMaxIngredients, pRecipe->iIngredientCount );  
  pRecipe = realloc( pRecipe , uAllocSize );  
  
  if (pRecipe != pRecipeOrg) { //realloc changed the address (SHOULD NOT HAPPEN!!!!!)
    DebugWarn("Realloc changed the address of the recipe");
    //update the hash table    
    Recipe* pCurrent = g_HashTableRecipes[uHash];
    if (pCurrent == pRecipeOrg) {
      g_HashTableRecipes[uHash] = pRecipe;
    } else {
      while (pCurrent != 0) {
        if (pCurrent->pNext == pRecipeOrg) {
          pCurrent->pNext = pRecipe;
          break;
        }
        pCurrent = pCurrent->pNext;
      }
    }    
  }
  return pRecipe;
}
void      RemoveRecipe( Recipe* pRecipe , uint32_t uHash ) { 
  Recipe* pCurrent = g_HashTableRecipes[uHash];  
  
  if (pCurrent == pRecipe) {
    g_HashTableRecipes[uHash] = pRecipe->pNext;
    free( pRecipe );
    return;
  }
  while (pCurrent != 0) {
    if (pCurrent->pNext == pRecipe) {
      pCurrent->pNext = pRecipe->pNext;
      free( pRecipe );
      return;
    }
    pCurrent = pCurrent->pNext;
  }
  DebugError("Recipe not found in hash table");
}
uint32_t  HashRecipeName(const char* pzStr, int iStrLen ) {
    uint32_t uHash = 5381;
    for (int i=0; i<iStrLen; i++) {
        uHash = ((uHash << 5) + uHash) + pzStr[i];
    }
    return uHash % HASH_TABLE_SIZE;
}

OrderQueue     *g_OrderQueue     = 0 , *g_OrderQueueEnd     = 0;
ToDeliverQueue *g_ToDeliverQueue = 0 , *g_ToDeliverQueueEnd = 0;

void QueueRecipe( Recipe* pRecipe , int32_t iQuantity , int32_t iOrderTime ) {
  OrderQueue* pOrder = allocate( sizeof(OrderQueue) );
  pOrder->pNext = 0;
  pOrder->pRecipe = pRecipe;
  pOrder->iQuantity = iQuantity;
  pOrder->iOrderTime = iOrderTime;
  //add this recipe to the end of the queue
  if (g_OrderQueue == 0) {
    g_OrderQueue = g_OrderQueueEnd = pOrder;
  } else {
    g_OrderQueueEnd->pNext = pOrder;
    g_OrderQueueEnd = pOrder;
  }
}
void AddToDeliveryQueue( Recipe* pRecipe , int32_t iOrderWeight , int32_t iQuantity , int32_t iOrderTime ) {
  //puts("<Immediate Delivery>");
  ToDeliverQueue* pOrder = allocate( sizeof(ToDeliverQueue) );
  pOrder->pNext = 0;
  pOrder->pRecipe = pRecipe;
  pOrder->iQuantity = iQuantity;
  pOrder->iTotalGrams = iOrderWeight;
  pOrder->iOrderTime = iOrderTime;
  //add this recipe to the end of the queue
  if (g_ToDeliverQueue == 0) {
    g_ToDeliverQueue = g_ToDeliverQueueEnd = pOrder;
  } else {
    g_ToDeliverQueueEnd->pNext = pOrder;
    g_ToDeliverQueueEnd = pOrder;
  }
}
void SortIntoDeliveryQueue( OrderQueue* pOrder , int32_t iOrderWeight ) {
  
  _Profile(g_iInsertions);
  
  ToDeliverQueue* pInsert = g_ToDeliverQueue;
  if (!pInsert) { //if empty, then add first
    g_ToDeliverQueue = g_ToDeliverQueueEnd = (ToDeliverQueue*)pOrder;        
    g_ToDeliverQueue->iTotalGrams = iOrderWeight;
    g_ToDeliverQueue->pNext = 0;
    return;
  } 
  
  //need an insertion sort to keep the delivery queue sorted in ordertime
  ToDeliverQueue* pPrevInsert = 0;
  while (pInsert) {
    //DebugWarn("Testing [%i %s %i] ? [%i %s %i]\n", pInsert->iOrderTime , (char*)(pInsert->pRecipe+1), pInsert->iQuantity , pOrder->iOrderTime , (char*)(pOrder->pRecipe+1), pOrder->iQuantity );
    _Profile(g_iInsertionsChecks);
    if ( (pInsert->iOrderTime) > (pOrder->iOrderTime) ) {
      ToDeliverQueue* pNew = (ToDeliverQueue*)pOrder;
      pNew->pNext = pInsert;
      pNew->iTotalGrams = iOrderWeight;        
      if (!pPrevInsert) { g_ToDeliverQueue = pNew; } else { pPrevInsert->pNext = pNew; }
      return;
    }     
    pPrevInsert = pInsert;
    pInsert = pInsert->pNext;
  }
  
  //if didnt found a place to insert, then insert at end
  if (!pInsert) {        
    ToDeliverQueue* pNew = (ToDeliverQueue*)pOrder;
    pNew->pNext = 0;
    pNew->iTotalGrams = iOrderWeight;
    g_ToDeliverQueueEnd = pPrevInsert->pNext = pNew;    
  }

}


RecipeIngredient* CanRecipeBePrepared( Recipe* pRecipe , int32_t iQuantity ) {  
  
  RecipeIngredient* pIngredients = (RecipeIngredient*)(((char*)(pRecipe+1))+pRecipe->iNameLength+1);
  int32_t iAdjustedTime = iCurrentTime;

  for ( int i=0 ; i < pRecipe->iIngredientCount ; i++) {
    
    //check if batch(es) expired
    Ingredient* pIngredient = pIngredients[i].pIngredient;                
    while ( (pIngredient->iGrams > 0) && (pIngredient->tBatch[pIngredient->iFirstBatch].iExpiration <= iAdjustedTime) ) {
      //remove expired batch and reduce grams
      //printf( "%i: Expired batch: %s %i", iCurrentTime , pIngredient->pzName, pIngredient->tBatch[pIngredient->iFirstBatch].iExpiration );
      pIngredient->iGrams -= pIngredient->tBatch[pIngredient->iFirstBatch].iGrams;
      pIngredient->iFirstBatch = (pIngredient->iFirstBatch+1) & (MAX_BATCHES-1);
    }

    //DebugWarn( "Ingreident: %s Grams: %i Batches: %i (need %i grams)", pIngredient->pzName, pIngredient->iGrams, pIngredient->iBatchCount, pIngredients[i].iGrams * iQuantity );

    if (pIngredient->iGrams < pIngredients[i].iGrams * iQuantity) {      
      return 0;
    }
  }  
  return pIngredients;
}
int32_t           SubtractIngredients( RecipeIngredient* pIngredients , int32_t iIngredientCount , int32_t iQuantity ) {  
  int32_t iOrderWeight = 0;
  for (int i=0; i<iIngredientCount; i++) {
    int32_t iIngredientGrams = pIngredients[i].iGrams*iQuantity;
    Ingredient* pIngredient = pIngredients[i].pIngredient;
    pIngredient->iGrams -= iIngredientGrams;
    iOrderWeight += iIngredientGrams;

    //remove quantities from batches
    while (iIngredientGrams) {
      if (iIngredientGrams >= (pIngredient->tBatch[pIngredient->iFirstBatch].iGrams)) {
        //full batch consumed
        iIngredientGrams -= pIngredient->tBatch[pIngredient->iFirstBatch].iGrams;
        pIngredient->iFirstBatch = (pIngredient->iFirstBatch+1) & (MAX_BATCHES-1);        
      } else {
        //partial batch consumed
        pIngredient->tBatch[pIngredient->iFirstBatch].iGrams -= iIngredientGrams;
        break;
      }      
    }
    
  }  
  return iOrderWeight;
}
void              PrepareRecipe( Recipe* pRecipe , int32_t iQuantity , int32_t iOrderTime ) {
  //check if we have enough ingredients (igredient list is after the name after the struct)
  pRecipe->iUsedCount++;

  RecipeIngredient* pIngredients = CanRecipeBePrepared( pRecipe , iQuantity );
  if (pIngredients == 0) {
    #ifdef DebugQueue
      printf("(queded) ");
    #endif
    QueueRecipe( pRecipe , iQuantity , iOrderTime );
    return;
  }
  
  //remove the ingredients from the stock
  int32_t iOrderWeight = SubtractIngredients( pIngredients , pRecipe->iIngredientCount , iQuantity );  
  AddToDeliveryQueue( pRecipe , iOrderWeight , iQuantity , iOrderTime );
  #ifdef DebugDelivery
  printf("(prepared) ");
  #endif
  
}
void              PrepareQuededOrders() {
  OrderQueue *pPrev=0 , *pOrder = g_OrderQueue;
  while (pOrder) {
    OrderQueue* pNext = pOrder->pNext;
    #ifdef DebugDelivery
      printf("Checking: %i %s %i\n", pOrder->iOrderTime , (char*)(pOrder->pRecipe+1), pOrder->iQuantity );
    #endif
    RecipeIngredient* pIngredients = CanRecipeBePrepared( pOrder->pRecipe , pOrder->iQuantity );
    
    //if we can prepare the recipe, then prepare it
    if (pIngredients) {
      
      #ifdef DebugDelivery
        printf("Late Delivery: %i %s %i\n", pOrder->iOrderTime , (char*)(pOrder->pRecipe+1), pOrder->iQuantity );
      #endif
      //remove the ingredients from the stock
      int32_t iOrderWeight = SubtractIngredients( pIngredients , pOrder->pRecipe->iIngredientCount , pOrder->iQuantity );      
      //insert in the delivery queue
      SortIntoDeliveryQueue( pOrder , iOrderWeight );
      
      //remove from queue (but dont free, because we moved it to the delivery queue)
      if (g_OrderQueueEnd==pOrder) {g_OrderQueueEnd = pPrev;} //if last then previous is new last
      if (!pPrev) {        
        g_OrderQueue = pNext; //if first, then new first is next
      } else { //if not then previous must point to next
        pPrev->pNext = pNext;
      }

    } else {
      pPrev = pOrder;
    }
    pOrder = pNext;
  }  
}

//sort by weight (bigger first) and then by order time (older first)
int     CompareDeliveryQueue( const void *ppA, const void *ppB ) {
  ToDeliverQueue* pA = *(ToDeliverQueue**)ppA;
  ToDeliverQueue* pB = *(ToDeliverQueue**)ppB;
  if (pA->iTotalGrams > pB->iTotalGrams) { return -1; }
  if (pA->iTotalGrams < pB->iTotalGrams) { return  1; }
  if (pA->iOrderTime  < pB->iOrderTime ) { return -1; }
  return 1; //because it's impossible to have two orders with the same time
  //if (pA->iOrderTime  > pB->iOrderTime ) { return  1; }
  //return 0; //never reached
}
int32_t LoadTruck() {
  //load the truck with the first orders
  int32_t iTotalWeight = 0, iDeliveryCount = 0;

  //check how many orders we can deliver
  ToDeliverQueue* pList = g_ToDeliverQueue;
  while (pList) {
    //ToDeliverQueue* pOrder = pList;
    if ((iTotalWeight + pList->iTotalGrams) > iTruckCapacity) {
      break;
    }
    iTotalWeight += pList->iTotalGrams;
    iDeliveryCount++;
    pList = pList->pNext;
  }
  //printf("Can Delivery %i orders", iDeliveryCount);
  if (!iDeliveryCount) { return 0; }

  //create a deliver list to be sorted
  ToDeliverQueue **pDeliveryList = allocate( sizeof(ToDeliverQueue*) * iDeliveryCount );
  for ( int i=0 ; i<iDeliveryCount ; i++ ) {
    pDeliveryList[i] = g_ToDeliverQueue;
    g_ToDeliverQueue = g_ToDeliverQueue->pNext;
  }
  
  //if all delivery were done then there's no "end" element 
  if (!g_ToDeliverQueue) { g_ToDeliverQueueEnd = 0; }
  
  //sort the delivery list by weight using qsort
  qsort( pDeliveryList , iDeliveryCount , sizeof(ToDeliverQueue*), CompareDeliveryQueue );

  //emit output
  for ( int i=0 ; i<iDeliveryCount ; i++ ) {
    ToDeliverQueue* pOrder = pDeliveryList[i];
    pOrder->pRecipe->iUsedCount--;
    emitF("%i %s %i\n", pOrder->iOrderTime , (char*)(pOrder->pRecipe+1), pOrder->iQuantity );
    free( pOrder );
  }
  free(pDeliveryList);
  
  return iDeliveryCount;
  
}

int main() {

  #define IsCommand(_cmd) (*(uint32_t*)pzToken) == (_cmd)
  //#define IsCommand(_cmd) (strncmp( line , (char*)&(_cmd) , sizeof(_cmd)-1 ) == 0)
  #define AdjustTextToken() pzToken[iTokenLen] = 0; pzLine = pzToken + iTokenLen;

  char line[MAX_LINE_LEN];

  //first line have truck period and capacity
  fgets(line, MAX_LINE_LEN, stdin);
  sscanf(line, "%d %d", &iTruckPeriod, &iTruckCapacity);
  iNextTruckTime = iTruckPeriod;  
  iCurrentTime = -1;
  
  while (1) {

    char *pzToken, *pzLine = line;
    int32_t iTokenLen;
    iCurrentTime++;  //start from 0

    #ifdef DebugIngredients
      //list all ingredients and quantities
      int iListed=0;
      char zBuffer[65536], *pzBuffer = zBuffer;
      for (int n=0 ; n<HASH_TABLE_SIZE; n++) {
        Ingredient* pIngredient = g_HashTableIngredients[n];
        while (pIngredient) {

          while ( (pIngredient->iGrams) && (pIngredient->tBatch[pIngredient->iFirstBatch].iExpiration <= iCurrentTime) ) {
            //remove expired batch and reduce grams
            printf( "Expired batch: %s %i\n", pIngredient->pzName, pIngredient->tBatch[pIngredient->iFirstBatch].iExpiration );
            pIngredient->iGrams -= pIngredient->tBatch[pIngredient->iFirstBatch].iGrams;
            pIngredient->iFirstBatch = (pIngredient->iFirstBatch+1) & (MAX_BATCHES-1);            
          }

          pzBuffer += sprintf(pzBuffer, "('%s'%i,%i) ", pIngredient->pzName, pIngredient->iGrams, pIngredient->iBatchCount );
          pIngredient = pIngredient->pNext; iListed=1;
        }      
      }
      if (iListed) { puts(zBuffer); }
    #endif

    #ifdef CheckQueue
    {
      ToDeliverQueue* pLast = g_ToDeliverQueue;
      while (pLast) {
          if (!pLast->pNext) break;
          pLast = pLast->pNext;
      }
      if ( g_ToDeliverQueueEnd != pLast ) {
          DebugError("Corrupted ToDeliverQueue");
      }
      
      OrderQueue* pLast0 = g_OrderQueue;
      while (pLast0) {
          if (!pLast0->pNext) break;
          pLast0 = pLast0->pNext;
      }
      if ( g_OrderQueueEnd != pLast0 ) {
          DebugError("Corrupted OrderQueue");
      }
    }
    #endif
            
    #ifdef DebugQueue
    {
      ToDeliverQueue* pList = g_ToDeliverQueue;
      while (pList) {
        printf("{%i %s %i} ", pList->iOrderTime , (char*)(pList->pRecipe+1), pList->iQuantity );
        pList = pList->pNext;
      }
      OrderQueue* pList0 = g_OrderQueue;
      while (pList0) {
        printf("<%i %s %i> ", pList0->iOrderTime , (char*)(pList0->pRecipe+1), pList0->iQuantity );
        pList0 = pList0->pNext;
      }
      if ( (g_OrderQueue) || (g_ToDeliverQueue) ) { puts(""); }
    }
    #endif

    if (iCurrentTime == iNextTruckTime) {
      //truck arrives
      if (!LoadTruck()) {
        emitS("camioncino vuoto");
      }     
      iNextTruckTime += iTruckPeriod;
    }

    if (fgets(line, MAX_LINE_LEN, stdin) == NULL) { break; }
    
    #ifdef DebugInput
      char *pTemp = memchr(line, '\n' , MAX_LINE_LEN);
      *pTemp = 0;
      //printf( "%i[%s] " , iCurrentTime , line );
      DebugWarn( "~%i[%s]\n" , iCurrentTime , line );     
      *pTemp = '\n';
    #endif
    
    iTokenLen = TextToken( pzLine , &pzToken );
    AdjustTextToken();
    Debug( "Cmd: (%i)'%s'" , iCurrentTime , pzToken );    

    if      (IsCommand( place_order   )) {      
      //Debug0("place_order");
      //ordine recipe quantity
      //replies: accettato (accepted) rifiutato (rejected (recipe does not exist)
      iTokenLen = TextToken( pzLine , &pzToken );
      AdjustTextToken();
      uint32_t uHash = HashRecipeName( pzToken , iTokenLen );
      Recipe* pRecipe = LocateRecipe( uHash , pzToken , iTokenLen );
      if (pRecipe == 0) {
        emitS("rifiutato");
        continue;
      }
      int32_t iQuantity;
      pzLine += NumToken( pzLine , &iQuantity);
      PrepareRecipe( pRecipe , iQuantity , iCurrentTime );
      emitS("accettato");      
    } 
    else if (IsCommand( replenishment )) {
      //Debug0("replenishment");
      //rifornimento ingredient amount expiration ...
      //replies: rifornito (replenished)
      //rifornimento farina 100 10 uova 100 10 zucchero 100 10 burro 100 10 latte 100 10 cioccolato 100 10
      int iEOL=' '; 
      Debug0( "\t\tReplenishing Ingredients:");
      while (iEOL == ' ') {        
        iTokenLen = TextToken( pzLine , &pzToken );
        AdjustTextToken();      
        int32_t iGrams, iExpiration;
        pzLine += NumToken( pzLine , &iGrams);
        pzLine += NumToken( pzLine , &iExpiration);
        iEOL = *pzLine; //after token we have space or end
        Debug0( "'%s'<%04X>(%i)" , pzToken , HashIngredientName(pzToken,iTokenLen) , iGrams );        
        ReplenishIngredient( pzToken , iTokenLen , iGrams , iExpiration );
      }  
      emitS("rifornito");
      PrepareQuededOrders(); //try to prepare the pending orders now
    } 
    else if (IsCommand( add_recipe    )) {      
      //Debug0("add_recipe");
      //aggiungi_ricetta recipe igredient amount
      //replies: aggiunta (added) oppure (??) ignorato (ignored, already exists)
      iTokenLen = TextToken( pzLine , &pzToken );            
      AdjustTextToken();
      uint32_t uHash = HashRecipeName( pzToken , iTokenLen );
      Debug( "\tRecipe: '%s'<%04X>" , pzToken, uHash );
      //check if recipe already exists
      if (LocateRecipe( uHash , pzToken , iTokenLen ) != 0) {
        Debug( "Recipe already exists" );
        emitS("ignorato");
        continue;
      }

      //count ingredients      
      int iIngredients = 0; //MAX_INGREDIENTS;
      for (char* pz = pzLine; *pz != '\n'; pz++) { if (*pz == ' ') { iIngredients++; } }
      iIngredients = (iIngredients+1)/2; //number of ingredients      
      Recipe* pRecipe = StartCreateRecipe( uHash , pzToken , iTokenLen , iIngredients );

      //read ingredients
      int iEOL=' ';
      Debug0( "\t\tIngredients:");
      while (iEOL == ' ') {
        iTokenLen = TextToken( pzLine , &pzToken );
        AdjustTextToken();
        int32_t iGrams;
        pzLine += NumToken( pzLine , &iGrams);
        iEOL = *pzLine; //after token we have space or end
        Debug0( "'%s'<%04X>(%i)" , pzToken , HashIngredientName(pzToken,iTokenLen) , iGrams );
        AddRecipeIngredient( pRecipe , pzToken , iTokenLen , iGrams );
      }      
      FinishCreateRecipe( pRecipe , uHash , iIngredients );
      Debug(" ");
      emitS("aggiunta");  
    } 
    else if (IsCommand( remove_recipe )) {
      Debug0("remove_recipe");
      iTokenLen = TextToken( pzLine , &pzToken );            
      AdjustTextToken();
      uint32_t uHash = HashRecipeName( pzToken , iTokenLen );
      Recipe* pRecipe = LocateRecipe( uHash , pzToken , iTokenLen );
      if (pRecipe == 0) {
        emitS("non presente");
        continue;
      }

      Debug0("Recipe: '%s' %i > ", pzToken, pRecipe->iUsedCount);

      if (pRecipe->iUsedCount > 0) {
        emitS("ordini in sospeso");
        continue;
      }
      //remove recipe
      RemoveRecipe( pRecipe , uHash );
      emitS("rimossa");
      
      //rimuovi_ricetta recipe
      //replies: non presente (not found) ordini in sospeso (have order placed, can't delete) rimossa (removed)
    } 
    else    { //bad line   
      DebugError("%s '%s'","Bad line found?", line);
    }        
  }

  #ifdef DebugHashTable
    int32_t iIngredientsHashConflicts = 0 , iRecipesHashConflicts = 0;
    int32_t iIngredientsMaxChain = 0 , iRecipesMaxChain = 0;
    for (int i=0; i<HASH_TABLE_SIZE; i++) {
      Ingredient* pIngredient = g_HashTableIngredients[i];
      if (pIngredient) {
        int iIngredientsChain = 0;
        while (1) {
          pIngredient = pIngredient->pNext;      
          if (pIngredient == 0) { break; }        
          iIngredientsChain++;
        }
        if (iIngredientsChain > iIngredientsMaxChain) { iIngredientsMaxChain = iIngredientsChain; }
        iIngredientsHashConflicts += iIngredientsChain;
      }

      Recipe* pRecipe = g_HashTableRecipes[i];
      if (pRecipe) {
        int iRecipesChain = 0;
        while (1) {
          pRecipe = pRecipe->pNext;      
          if (pRecipe == 0) { break; }        
          iRecipesChain++;
        }
        if (iRecipesChain > iRecipesMaxChain) { iRecipesMaxChain = iRecipesChain; }
        iRecipesHashConflicts += iRecipesChain;
      }  
      
    }
    DebugWarn("Hash conflicts: Ingredients %i (max=%i) Recipes %i (max=%i)", 
    iIngredientsHashConflicts, iIngredientsMaxChain, iRecipesHashConflicts, iRecipesMaxChain );
  #endif
  
  #ifdef DebugProfile
    DebugWarn( "Batches=%i IgnoredBatched=%i BatchMergings: %i",g_iBatches , g_iIgnoredBatches , g_iBatchMergings );
    DebugWarn( "Insertions: %i InsertionChecks: %i Avg: %i",g_iInsertions,g_iInsertionsChecks,g_iInsertions ? g_iInsertionsChecks/g_iInsertions : 0 );
  #endif
  
  #ifdef DebugUsage
    #ifdef _WIN32
      freopen("con","w",stdout);   
      #define cmd "wmic process where name='loadtruck.exe' get KernelModeTime,PrivatePageCount,UserModeTime"
      FILE* pf = popen( cmd , "r" );
      fgets(line, MAX_LINE_LEN, pf); fgets(line, MAX_LINE_LEN, pf);
      uint32_t uKernel,uPages,uUser;
      sscanf(line,"%i %i %i",&uKernel,&uPages,&uUser);
      printf("time=%1.3fs memory=%1.2fmb\n",(uKernel+uUser)/10000000.0,uPages/(1024*1024.0));
      fclose(pf);  
      //freopen("con", "r", stdin); getchar(); //pause
    #else //linux?
      //TODO: implement (if needed)
    #endif

  #endif
}

#if 0
/*
    AZ az 09 _
    
    when truck arrives 
    there's nothing to send: camioncino vuoto
    otherwise order list   : when_ordered(0) recipe amount  (bigger amount first)
    -------------------------------------------------------------------

    addrecipe        
    aggiungi_ricetta recipe igredient amount

    replies: 
        aggiunta (added) 
        oppure   (??)
        ignorato (ignored, already exists)
    
    -------------------------------------------------------------------
    
    remove recipe
    rimuovi_ricetta recipe
    
    replies:
        non presente      (not found)
        ordini in sospeso (have order placed, can't delete)
        rimossa           (removed)
        
    -------------------------------------------------------------------
    
    replenishment
    rifornimento ingredient amount expiration ...
    
    replies:
        rifornito (replenished)
        
    -------------------------------------------------------------------
    
    place order
    ordine recipe quantity
    
    replies:
        accettato (accepted)
        rifiutato (rejected (recipe does not exist)
        
    -------------------------------------------------------------------
*/  
#endif