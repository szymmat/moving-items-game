#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <ftw.h>
#include <signal.h>


#define MAX_PATH 1000
#define MAX_GRAPH 1000
#define MAX_FD 20

// A macro that describes an error and its source, if occured.
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

// A macro that computes time between end and start.
#define ELAPSED(start,end) ((end).tv_sec-(start).tv_sec)+(((end).tv_nsec - (start).tv_nsec) * 1.0e-9)

// A structure containing game data.
typedef struct gameArgs {
    bool** graph; // Game map.
    bool* properRoom; // // Array containing information which items are in their destination rooms.
    int movesCount; // Number of moves made by player.
    int roomCount; // Number of rooms on the map.
    int itemHeld; // Number of item held by a player or -1 if player does not hold any item.
    int currentRoom; // Number of currently visited room.
    int saveCount; // Number of manual game saves.
    pthread_mutex_t* mutex; // Mutex for synchronization.
    sigset_t* mask; // Signal mask.
    int* items; // Array containing current location of every item.
    int* itemDest; // Array containing destination location of every item.
    int* itemsInRoom; // Array containing number of items in every room.
    char* autosavePath; // Path for autosave.
} gameArgs_t;

// Structure passed to threads executing FindPath (finding path between two rooms)
typedef struct findPathArgs { 
    gameArgs_t* args; // Current game status.
    int x; // Number of destination room for a path.
} findPathArgs_t;

// Structure returned from FindPath (finding path between two rooms)
typedef struct findPathReturnArgs { 
    int* path; // Path found.
    int len; // Path length.
    bool ok; // Information if found path ends in destination room.
} findPathReturnArgs_t;

// Structure modified by nftw function called by MapFromDirTree (creating map from a directory tree).
typedef struct nftwArgs { 
    int level[MAX_PATH]; // Depth of every found directory.
    int len; // Number of found directories.
} nftwArgs_t;

nftwArgs_t nftwArgs; // A global variable essential to return data from nftw.

// Prints a proper way to execute program. 
void usage() { 
    perror("USAGE: ./main -b backup-path\n");
    exit(EXIT_FAILURE);
}

// Creates and returns undirected graph with n vertices and random edges between them, which becomes game map.
bool** CreateGraph(int n) { 
    bool** graph = (bool **) malloc(sizeof(bool *) * n);
    if(!graph) ERR("malloc");
    int i,j;
    for(i=0;i<n;i++) {
        graph[i] = (bool*)malloc(sizeof(bool)*n);
        if(!graph[i]) ERR("malloc");
    }
    for(i=0;i<n-1;i++) {
        int count = 0; // We need to ensure the graph is connected.
        while(count == 0) {
            for(j=i+1;j<n;j++) {
                int num = rand()%2;
                if(num == 1) {
                    graph[i][j] = true;
                    graph[j][i] = true;
                    count++;
                }
                else {
                    graph[i][j] = false;
                    graph[j][i] = false;
                }
            }
        }
    }
    return graph;
}

// Saves graph with n vertices to a file.
void SaveGraph(bool** graph, int n, char* path) {
    int out,i,j;
    if((out=open(path,O_WRONLY|O_CREAT|O_TRUNC,0777))<0)ERR("open");
    for(i=0;i<n;i++) {
        for(j=0;j<n;j++) {
            dprintf(out,"%d",graph[i][j]);
        }
    }
    if(close(out)) ERR("close");
}

// Reads map from file into args structure.
void ReadGraph(gameArgs_t* args, char* path) {
    int in;
    int i,j;
    if((in=open(path,O_RDONLY))<0)ERR("open");
    char buf [MAX_PATH] = "";
    args->roomCount = (int) sqrt(read(in,buf,MAX_GRAPH));
    args->graph = (bool**) malloc(sizeof(bool*) * args->roomCount);
    if(!args->graph) ERR("malloc");
    for(i=0;i<args->roomCount;i++) {
        args->graph[i] = (bool*) malloc(sizeof(bool));
        if(!args->graph[i]) ERR("malloc");
        for(j=0;j<args->roomCount;j++) {
            if(buf[i*args->roomCount+j] == '0') args->graph[i][j] = false;
            else args->graph[i][j] = true;
        }
    }
    if(close(in)) ERR("close");
}

// Preparing new game.
void OrganizeItems(gameArgs_t* args) { 
    int i;
    int numberOfItems = 3*args->roomCount/2;
    args->properRoom = (bool*) malloc(sizeof(bool)*numberOfItems);
    args->movesCount = 0;
    args->saveCount = 0;
    args->items = (int*) malloc(sizeof(int)*numberOfItems);
    args->itemsInRoom = (int*) malloc(sizeof(int)*args->roomCount);
    args->itemDest = (int*) malloc(sizeof(int)*numberOfItems);
    int* destsInRoom = (int*)malloc(sizeof(int)*args->roomCount);
    args->itemHeld = -1;
    args->currentRoom = rand()%args->roomCount;
    if(!args->items || !args->itemsInRoom || !args->itemDest || !destsInRoom || !args->properRoom) ERR("malloc");
    for(i=0;i<args->roomCount;i++) {
        args->itemsInRoom[i] = 0;
        destsInRoom[i] = 0;
    }
    for(i=0;i<numberOfItems;i++) { // Assigning start location to items.
        bool placed = false;
        args->properRoom[i] = false;
        while(!placed) {
            int roomNumber = rand()%args->roomCount;
            if(args->itemsInRoom[roomNumber] < 2) {
                args->items[i] = roomNumber;
                args->itemsInRoom[roomNumber]++;
                placed = true;
            }
        }
    }
    for(i=0;i<numberOfItems;i++) { // Assigning destination rooms to items.
        bool placed = false;
        while(!placed) {
            int destRoomNumber = rand()%args->roomCount;
            if(destsInRoom[destRoomNumber] < 2 && destRoomNumber != args->items[i]) {
                args->itemDest[i] = destRoomNumber;
                destsInRoom[destRoomNumber]++;
                placed = true;
            }
        }
    }
    free(destsInRoom);
}

// Prints rooms currently available to move into.
void PrintAvailableRooms(gameArgs_t* args) {
    printf("Current room is %d. Available rooms are: ", args->currentRoom);
    for(int i=0;i<args->roomCount;i++) {
        if(args->graph[args->currentRoom][i]) printf("%d ",i);
    }
    printf("\n");
}

// Prints basic information about current game.
void PrintItemsInRoom(gameArgs_t* args) {
    printf("Items in this room: ");
    int i;
    for(i=0;i<3*args->roomCount/2;i++) {
        if(args->items[i] == args->currentRoom && i != args->itemHeld) printf("%d ",i);
    }
    printf("\nProperly placed items are: ");
    for(i=0;i<3*args->roomCount/2;i++) {
        if(args->properRoom[i]) printf("%d ",i);
    }
    printf("\n");
    if(args->itemHeld == -1) printf("You can pick up an item.");
    else printf("You are carrying item %d to room %d.",args->itemHeld,args->itemDest[args->itemHeld]);
    printf(" Moves count: %d\n",args->movesCount);
}

// Checks if the game had finished.
bool CheckIfFinished(gameArgs_t* args) {
    int i;
    for(i=0;i<3*args->roomCount/2;i++) {
        if(args->properRoom[i] == false) return false;
    }
    return true;
}

// Saves game data to file pointed to by path.
void SaveGame(gameArgs_t* args, char* path) {
    int out,i;
    int numberOfItems = 3*args->roomCount/2;
    SaveGraph(args->graph,args->roomCount,path);
    if((out=open(path,O_WRONLY|O_CREAT|O_APPEND,0777))<0)ERR("open");
    dprintf(out," ");
    for(i=0;i<numberOfItems;i++) {
        dprintf(out,"%d",args->properRoom[i]);
    }
    dprintf(out," ");
    dprintf(out, "%d ",args->movesCount);
    dprintf(out,"%d ",args->roomCount);
    dprintf(out,"%d ",args->itemHeld);
    dprintf(out,"%d ",args->currentRoom);
    for(i=0;i<numberOfItems;i++) {
        dprintf(out,"%d ",args->items[i]);
    }
    for(i=0;i<numberOfItems;i++) {
        dprintf(out,"%d ",args->itemDest[i]);
    }
    for(i=0;i<args->roomCount;i++) {
        dprintf(out,"%d ",args->itemsInRoom[i]);
    }
}

// Reads numbers from char array pointed to by buf at bufPosition.
int ReadValue(int* bufPosition, char* buf) {
    (*bufPosition)++;
    int movesCount;
    if(buf[(*bufPosition)+1] != ' ') { // If a number is 2-digit or -1
        if(buf[*bufPosition] == '-') movesCount = -1;
        else movesCount = 10*buf[*bufPosition]+buf[(*bufPosition)+1];
        (*bufPosition)+=2;
    }
    else { // If a number is 1-digit
        movesCount = buf[*bufPosition];
        (*bufPosition)++;
    }
    return movesCount;
}

// Loads game data from file pointed to by path.
void LoadGame(gameArgs_t* args, char* path) {
    int in,i,j;
    int bufPosition = 0;
    if((in=open(path,O_RDONLY))<0)ERR("open");
    char buf[MAX_PATH] = "";
    read(in,buf,MAX_PATH);
    if(close(in)) ERR("close");
    for(i=0;i<MAX_PATH;i++) {
        if(buf[i] == ' ') break;
        bufPosition++;
    }
    args->saveCount = 0;
    args->roomCount = sqrt(bufPosition);
    int numberOfItems = 3*args->roomCount/2;
    args->graph = (bool**)malloc(sizeof(bool*)*args->roomCount);
    args->properRoom = (bool*)malloc(sizeof(bool)*numberOfItems);
    args->items = (int*) malloc(sizeof(int)*numberOfItems);
    args->itemsInRoom = (int*) malloc(sizeof(int)*args->roomCount);
    args->itemDest = (int*) malloc(sizeof(int)*numberOfItems);
    if(!args->graph || !args->properRoom || !args->items || !args->itemsInRoom || !args->itemDest) ERR("malloc");
    for(i=0;i<args->roomCount;i++) {
        args->graph[i] = (bool*) malloc(sizeof(bool));
        if(!args->graph[i]) ERR("malloc");
        for(j=0;j<args->roomCount;j++) {
            if(buf[i*args->roomCount+j] == '0') args->graph[i][j] = false;
            else args->graph[i][j] = true;
        }
    }
    bufPosition++;
    for(i=0;i<MAX_PATH;i++) {
        if(buf[i] >= 48) buf[i]-=48;
    }
    for(i=0;i<numberOfItems;i++) {
        args->properRoom[i] = buf[bufPosition];
        bufPosition++;
    }
    args->movesCount = ReadValue(&bufPosition,buf);
    args->roomCount = ReadValue(&bufPosition,buf);
    args->itemHeld = ReadValue(&bufPosition,buf);
    args->currentRoom = ReadValue(&bufPosition,buf);
    for(i=0;i<numberOfItems;i++) args->items[i] = ReadValue(&bufPosition,buf);
    for(i=0;i<numberOfItems;i++) args->itemDest[i] = ReadValue(&bufPosition,buf);
    for(i=0;i<args->roomCount;i++) args->itemsInRoom[i] = ReadValue(&bufPosition,buf);
}

// Function passed to threads executing FindPath.
void* FindingPath(void* pVoid) {
    findPathArgs_t* findArgs = pVoid;
    int roomCount = findArgs->args->roomCount;
    int currentRoom = findArgs->args->currentRoom;
    findPathReturnArgs_t* returnArgs = (findPathReturnArgs_t*)malloc(sizeof(findPathReturnArgs_t)*roomCount);
    if(!returnArgs) ERR("malloc");
    returnArgs->path = (int*)malloc(sizeof(int)*roomCount);
    if(!returnArgs->path) ERR("malloc");
    returnArgs->path[0] = currentRoom;
    returnArgs->len = 0;
    int i,j;
    for(i=1;i<roomCount;i++) returnArgs->path[i] = -1;
    int maxSteps = 1000;
    for(i=0;i<maxSteps;i++) {
        if(returnArgs->path[returnArgs->len] == findArgs->x) break;
        int num = rand()%roomCount;
        if(findArgs->args->graph[currentRoom][num]) {
            for(j=0;j<=returnArgs->len;j++) {
                if(returnArgs->path[j] == num) break;
                if(j == returnArgs->len) {
                    returnArgs->len++;
                    returnArgs->path[returnArgs->len] = num;
                    currentRoom = num;
                }
            }
        }
    }
    returnArgs->ok = (returnArgs->path[returnArgs->len] == findArgs->x);
    return returnArgs;
}

// Tries to find shortest path to room x using k threads (this solution is far from optimal, but it was required to do it that way)
findPathReturnArgs_t* FindPath(gameArgs_t* args, int k, int x) {
    pthread_t* pthreads = (pthread_t*)malloc(sizeof(pthread_t)*k);
    if(!pthreads) ERR("malloc");
    findPathArgs_t findPathArgs;
    void* res;
    findPathArgs.args = args;
    findPathArgs.x = x;
    findPathReturnArgs_t* returnArgs1 = (findPathReturnArgs_t*)malloc(sizeof(findPathReturnArgs_t));
    if(!returnArgs1) ERR("malloc");
    returnArgs1->path = (int*)malloc(sizeof(int)*(args->roomCount+1));
    if(!returnArgs1->path) ERR("malloc");
    int i;
    int pathLength = args->roomCount+1;
    for(i=0;i<k;i++) {
        if(pthread_create(&pthreads[i],NULL,FindingPath,&findPathArgs)) ERR("pthread_create");
    }
    for(i=0;i<k;i++) {
        if(pthread_join(pthreads[i],&res)) ERR("pthread_join");
        findPathReturnArgs_t* returnArgs = res;
        if(returnArgs->ok) {
            if(returnArgs->len < pathLength) returnArgs1 = returnArgs;
        }
    }
    free(pthreads);
    return returnArgs1;
}

// Frees dynamically allocated memory.
void FreeMemory(gameArgs_t *args) {
    int i;
    for(i=0;i<args->roomCount;i++) free(args->graph[i]);
    free(args->graph);
    free(args->items);
    free(args->itemDest);
    free(args->properRoom);
    free(args->itemsInRoom);
    pthread_mutex_unlock(args->mutex);
}

// Main game function.
void Play(gameArgs_t* args) {
    char text[MAX_PATH] = "", *cmd, *arg1, *arg2;
    PrintAvailableRooms(args);
    PrintItemsInRoom(args);
    while(1) {
        fgets(text,MAX_PATH,stdin);
        cmd = strtok(text," ");
        if(cmd[strlen(cmd)-1] == '\n') cmd[strlen(cmd)-1] = '\0'; // fgets leaves \n, we need to deal with it
        arg1 = strtok(NULL," ");
        if(arg1 && arg1[strlen(arg1)-1] == '\n') arg1[strlen(arg1)-1] = '\0';
        arg2 = strtok(NULL, " ");
        if(arg2 && arg2[strlen(arg2)-1] == '\n') arg2[strlen(arg2)-1] = '\0';
        pthread_mutex_lock(args->mutex);
        if(strcmp(cmd,"quit") == 0) { // quit
            FreeMemory(args);
            return;
        }
        if(strcmp(cmd,"move-to") == 0 && arg1) { // move-to
            int tmp = atoi(arg1);
            if(args->graph[args->currentRoom][tmp]) args->currentRoom = tmp;
            else printf("Bad room number\n");
        }
        else if(strcmp(cmd,"pick-up") == 0 && arg1) { // pick-up
            int tmp = atoi(arg1);
            if(args->itemHeld == -1 && args->items[tmp] == args->currentRoom) {
                args->itemHeld = tmp;
                args->items[args->itemHeld] = -1;
                args->itemsInRoom[args->currentRoom]--;
            }
        }
        else if(strcmp(cmd,"drop") == 0 && arg1) { // drop
            int tmp = atoi(arg1);
            if(tmp == args->itemHeld && args->itemHeld != -1 && args->itemsInRoom[args->currentRoom] <= 1) {
                args->items[tmp] = args->currentRoom;
                args->itemsInRoom[args->currentRoom]++;
                args->itemHeld = -1;
                if(args->items[tmp] == args->itemDest[tmp]) args->properRoom[tmp] = true;
                else args->properRoom[tmp] = false;
                if(CheckIfFinished(args)) {
                    printf("Finished game with %d moves\n",args->movesCount);
                    FreeMemory(args);
                    return;
                }
            }
        }
        else if(strcmp(cmd,"save") == 0 && arg1) { // save
            SaveGame(args,arg1);
            args->saveCount++;
        }
        else if(strcmp(cmd,"find-path") == 0 && arg1 && arg2) { // find-path
            int k = atoi(arg1);
            int x = atoi(arg2);
            int i;
            findPathReturnArgs_t* returnArgs = FindPath(args,k,x);
            if(!returnArgs->ok) printf("Couldn't find path\n");
            else {
                for(i=0;i<=returnArgs->len;i++) printf("%d ",returnArgs->path[i]);
            }
            free(returnArgs);
            printf("\n");
        }
        args->movesCount++; // every command adds to the score, even if it's invalid
        pthread_mutex_unlock(args->mutex);
        PrintAvailableRooms(args);
        PrintItemsInRoom(args);
    }
}

// Suspends thread for a given time in milliseconds.
void msleep(unsigned int milisec) {
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    struct timespec req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) ERR("nanosleep");
}

// Function passed to autosave thread, saves game data after a minute passed from last save.
void* autosaveWork(void* args) {
    gameArgs_t* args1 = args;
    struct timespec start,current;
    int currSaveCount = args1->saveCount;
    if (clock_gettime(CLOCK_REALTIME, &start)) ERR("clock_gettime");
    while(1) {
        do {
            msleep(1000);
            if(currSaveCount != args1->saveCount) {
                if (clock_gettime(CLOCK_REALTIME, &start)) ERR("clock_gettime");
                currSaveCount = args1->saveCount;
            }
            else if (clock_gettime(CLOCK_REALTIME, &current)) ERR("clock_gettime");
        } while (ELAPSED(start,current) < 60.0);
        pthread_mutex_lock(args1->mutex);
        printf("\nAutosaving to %s\n",args1->autosavePath);
        SaveGame(args1,args1->autosavePath);
        pthread_mutex_unlock(args1->mutex);
        if (clock_gettime(CLOCK_REALTIME, &start)) ERR("clock_gettime");
    }
}

// Argument of nftw called from MapFromDirTree.
int walk(const char *name, const struct stat *s, int type, struct FTW *f) {
    if(type == FTW_D) {
        nftwArgs.level[nftwArgs.len] = f->level;
        nftwArgs.len++;
    }
    return 0;
}

// Creates map from directory tree represented by NnftwArgs.
bool** MapFromDirTree(nftwArgs_t* NnftwArgs) {
    int i,j;
    bool** graph = (bool**)malloc(sizeof(bool*) * NnftwArgs->len);
    if(!graph) ERR("malloc");
    for(i=0;i<NnftwArgs->len;i++) {
        graph[i] = (bool*)malloc(sizeof(bool)*NnftwArgs->len);
        if(!graph[i]) ERR("malloc");
    }
    for(i=0;i<NnftwArgs->len;i++) {
        for(j=0;j<NnftwArgs->len;j++) graph[i][j] = false;
    }
    int LastOfLevel[MAX_FD];
    LastOfLevel[0] = 0;
    int currLevel = 0;
    for(i=1;i<NnftwArgs->len;i++) {
        if(NnftwArgs->level[i] != currLevel) currLevel = NnftwArgs->level[i];
        LastOfLevel[currLevel] = i;
        graph[LastOfLevel[currLevel-1]][i] = true;
        graph[i][LastOfLevel[currLevel-1]] = true;
    }
    return graph;
}

// Function passed to thread waiting for SIGUSR1, after receiving SIGUSR1 swaps rooms between two items.
void* signalHandling(void* pVoid) {
    gameArgs_t* args = pVoid;
    int signo;
    while(1) {
        if(sigwait(args->mask,&signo)) ERR("sigwait");
        if(signo == SIGUSR1) {
            int a=-1,b=-1;
            while(a == b || a == args->itemHeld || b == args->itemHeld) {
                a = rand()%(3*args->roomCount/2);
                b = rand()%(3*args->roomCount/2);
            }
            int k = args->items[a];
            pthread_mutex_lock(args->mutex);
            args->items[a] = args->items[b];
            args->items[b] = k;
            if(args->items[a] == args->itemDest[a]) args->properRoom[a] = true;
            else args->properRoom[a] = false;
            if(args->items[b] == args->itemDest[b]) args->properRoom[b] = true;
            else args->properRoom[b] = false;
            pthread_mutex_unlock(args->mutex);
            printf("Swapped item %d with item %d.\n",a,b);
            PrintAvailableRooms(args);
            PrintItemsInRoom(args);
        }
    }
}

// Main menu.
int main(int argc, char** argv) {
    if(argc != 1 && argc != 3) usage();
    char savePath[MAX_PATH] = "";
    char text[MAX_PATH] = "", *cmd, *arg1, *arg2;
    gameArgs_t args;
    srand(time(NULL));
    if(argc == 1) {
        if(getenv("GAME_AUTOSAVE")) strcpy(savePath,getenv("GAME_AUTOSAVE"));
        else strcpy(savePath,"./.game_autosave");
    }
    else if(strcmp(argv[1],"-b") != 0) usage();
    else strcpy(savePath,argv[2]);
    args.autosavePath = savePath;
    pthread_t autosave, signalHandler;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    args.mutex = &mutex;
    sigset_t mask,oldmask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGUSR1);
    if(pthread_sigmask(SIG_BLOCK,&mask,&oldmask)) ERR("pthread_sigmask");
    args.mask = &mask;
    while(1) {
        fgets(text,MAX_PATH,stdin);
        cmd = strtok(text," ");
        if(cmd[strlen(cmd)-1] == '\n') cmd[strlen(cmd)-1] = '\0'; // Dealing with \n left by fgets.
        arg1 = strtok(NULL," ");
        if(arg1 && arg1[strlen(arg1)-1] == '\n') arg1[strlen(arg1)-1] = '\0';
        arg2 = strtok(NULL," ");
        if(arg2 && arg2[strlen(arg2)-1] == '\n') arg2[strlen(arg2)-1] = '\0';
        if(strcmp(cmd,"exit") == 0) break; // exit command
        if(strcmp(cmd,"generate-random-map") == 0 && arg1 && arg2) { // generate-random-map command
            args.roomCount = atoi(arg1);
            args.graph = CreateGraph(args.roomCount);
            SaveGraph(args.graph,args.roomCount,arg2);
        }
        else if(strcmp(cmd,"map-from-dir-tree") == 0 && arg1 && arg2) { // map-from-dir-tree command
            nftwArgs.len = 0;
            if(nftw(arg1,walk,MAX_FD,FTW_PHYS)) ERR("nftw");
            args.roomCount = nftwArgs.len;
            args.graph = MapFromDirTree(&nftwArgs);
            SaveGraph(args.graph,args.roomCount,arg2);
        }
        else if (strcmp(cmd,"load-game") == 0 && arg1) { // load-game command
            LoadGame(&args,arg1);
            if(pthread_create(&autosave,NULL,autosaveWork,&args)) ERR("pthread_create");
            if(pthread_create(&signalHandler,NULL,signalHandling,&args)) ERR("pthread_create");
            Play(&args);
            pthread_cancel(autosave);
            pthread_cancel(signalHandler);
        }
        else if(strcmp(cmd,"start-game") == 0 && arg1) { // save-game command
            ReadGraph(&args,arg1);
            OrganizeItems(&args);
            if(pthread_create(&autosave,NULL,autosaveWork,&args)) ERR("pthread_create");
            if(pthread_create(&signalHandler,NULL,signalHandling,&args)) ERR("pthread_create");
            Play(&args);
            pthread_cancel(autosave);
            pthread_cancel(signalHandler);
        }
        else printf("Bad command\n");
    }
    return EXIT_SUCCESS;
}
