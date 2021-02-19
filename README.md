# moving-items-game
My biggest project so far, a game focused on moving items to proper rooms under certain conditions. Written in C for Linux as a project for my Operating Systems course.

**1 Overview**
The game takes place on a map, which is an undirected graph of rooms. In each room there are no more than two items. The aim of the game is to move all items to the rooms to which they are assigned, while making as few moves as possible. At the beginning of the game the player is located in a random room and carries no items. The player may take the following actions:
1.	go to any room that is adjacent to the current,
2.	pick up any item in the currently visited room, provided he or she carries no more than one item.
3.	place any item he or she carries in the current room, if there is no more than one item in that room.
Each item has its own unique number and the room to which it is assigned (this is NEVER the room where it is initially located). 

**2 Launch of the program**
The program runs with the following option: 
**-b backup-path**
where backup-path is the path to the file where saved game data will be stored. This option is not mandatory. If not specified, the value of the environment variable $GAME_AUTOSAVE is used instead. If this variable is not set, the  .game-autosave file in the current directory is used.

**3 Player Commands**
The program waits for commands in two modes: main menu mode and game mode. Initially, the program waits for commands in main menu mode. 

**3.1 Main menu**
The main menu provides the following commands: 

**map-from-dir-tree path save-path**

generates a map from directory tree rooted at path. Each directory becomes a room, and edges are between parent directory and child directory. The result is saved to save-path.

**generate-random-map n path**

generates a random map composed n rooms connected in a random way. The result is saved to path. The map is always a connected graph.

**start-game path**
launches a new game on the map saved to path

**load-game path**
loads game data from path  (both map and item status, player status, number of moves etc.).

**exit**
quits the program

**3.2 During game**

**move-to x**
the player moves to room x, if possible.

**pick-up y**
the player picks up the item y if it does not violate the rules from paragraph 1.

**drop z**
the player drops the item z if it does not violate the rules from paragraph 1.

**save path**
Saves current game data to path.

**find-path k x**
The task stated that search had to be implemented in the following foolish way: 😊 
•	create k threads
•	each thread looks for a route starting from the current vertex and in a loop passes to a random neighbor. The loop ends either after reaching x or after a limit of 1000 steps.
•	Wait for the threads to finish.
The result is the route with fewest steps. If no thread has reached x, player receives a message that the route was not found.
This method does not guarantee that the found route is optimal, so the player should rather rely on his or her memory while playing (that’s clearly an advantage of that strange method of finding shortest path 😊) 

**quit**
Quits to main menu.

**4 Launch of a new game**
Launching a new game involves loading the map from the file. Let n be number of rooms on the map. 3n/2 items are then added to the map. Each item is placed in a random room with the limit of 2 items per room. For each item, the room to which it is assigned is also drawn with the same limit. The player is placed in a random room and carries no items. 

**5 Autosave**
At the start of the game, the autosave thread is started. If more than 60 seconds have passed since the last save of the game (either auto-save or save command), the current state of the game is saved to the backup-path file. During autosave, the game does not accept any commands from the player and a message is displayed. 

**6 Signal handling**
After the game is started, a signal handling thread is also started and it waits for SIGUSR1 signal. In response to SIGUSR1 signal, the thread replaces two randomly selected items in the game and prints the corresponding message.
