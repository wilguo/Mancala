#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */ //80
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 50720;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1]; 
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    struct player *next;
    struct player *prev;
    int after;
    char buf[MAXNAME + 1];
};
struct player *playerlist = NULL;
struct player *current_player = NULL;
struct player *first_player = NULL;
struct player *pending_players = NULL;
int flag_continue = 0;
int same_name_flag = 0;


extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

struct player *accept_connection(int fd);
int is_unique_name(char *name);
void get_player_display(struct player *p);
void alert_move(struct player *cur_player);
void alert_disconnection(char *name);
void make_move(int pit_num, struct player *p);
void broadcast_gameboard();
int find_network_newline(const char *buf, int n);



int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    while (!game_is_over()) {

        fd_set listen_fds = all_fds;

        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(listenfd, &listen_fds)) {
            struct player *curr_player = accept_connection(listenfd);

            //Prompt new player for name
            char welcome_msg[] = "Welcome to Mancala. What is your name?\r\n";
            write(curr_player->fd, welcome_msg, strlen(welcome_msg));

            //Add the new players to pending_players
            if(pending_players == NULL){
                pending_players = curr_player;
                curr_player->next = NULL;
                curr_player->prev = NULL;
            }else{              
                pending_players->prev = curr_player;
                curr_player->next = pending_players;
                curr_player->prev = NULL;
                pending_players = curr_player; 
            }

            //Add new pending player to fd_set
            if (curr_player->fd > max_fd) {
                max_fd = curr_player->fd;
            }
            FD_SET(curr_player->fd, &all_fds); 
        }

        // Players who are still in the process of typing in their name
        for (struct player *p = pending_players; p; p = p->next) {
            if (FD_ISSET(p->fd, &listen_fds)) {

                //Read name from new player
                char user_input[MAXNAME + 1];
                int num_read;
                num_read = read(p->fd, &user_input, MAXNAME);

                //Player has disconnected before typing in full name
                if(num_read == 0){
                    printf("New player has disconnected.\n");

                    //Remove new player from pending_players
                    if(p->prev == NULL && p->next == NULL){
                        //Only one player in pending_players
                        pending_players = NULL;
                    }
                    else if(p->prev == NULL && p->next != NULL){
                        //Remove most recently added pending player
                        pending_players = p->next;
                        pending_players->prev = NULL;
                    }             
                    else if(p->prev != NULL && p->next == NULL){
                        //Remove oldest added pending player
                        p->prev->next = NULL;
                    }
                    else if(p->prev != NULL && p->next != NULL){
                        //Remove middle player from pending_players
                        p->prev->next = p->next;
                        p->next->prev = p->prev;
                    }
                    FD_CLR(p->fd, &all_fds);
                    // Don't go onto loop for checking players in playerlist
                    // Rather refresh and continue onto next iteration of while loop
                    flag_continue = 1;
                }

                //Player did not disconnect
                else{    
                    p->after += num_read;       //Update last character index
                    strcat(p->buf, user_input); //Add user input to buf

                    // Check if user has typed in a complete name
                    int is_complete = 0;
                    is_complete = find_network_newline(p->buf, strlen(p->buf));

                    //User has typed a complete name
                    if(is_complete >= 0){
                        p->buf[is_complete] = '\0';

                        //Delete \r\n to save into p->name late
                        for(int i = 0; i < strlen(p->buf); i++){
                            if(p->buf[i] == '\r' || p->buf[i] == '\n'){
                                p->buf[i] = '\0';
                            }
                        }

                        //Check if name already exists
                        int name_count = is_unique_name(p->buf);

                        // There already exists that name
                        if(name_count != 0){
                            same_name_flag = 1;
                            char name_msg[] = "Name already exists. Please try again.\r\n";
                            write(p->fd, name_msg, strlen(name_msg));
                            strcpy(p->buf, "");
                        }
                        // Name does not exist but name is too long
                        else if(is_complete > MAXNAME){
                            same_name_flag = 1;
                            char name_msg[] = "Name too long. Please try again.\r\n";
                            write(p->fd, name_msg, strlen(name_msg));
                            strcpy(p->buf, "");
                        }


                        // If passed both distinct name test and length test
                        if(same_name_flag == 0){
                            strcpy(p->name, p->buf);

                            //Remove new player from pending_players                 
                            if(p->prev == NULL && p->next == NULL){
                                //Only one player in pending_players
                                pending_players = NULL;
                            }          
                            else if(p->prev == NULL && p->next != NULL){
                                //Remove most recently added pending player
                                pending_players = p->next;
                                pending_players->prev = NULL;
                            }    
                            else if(p->prev != NULL && p->next == NULL){
                                //Remove oldest added pending player
                                p->prev->next = NULL;
                            }
                            else if(p->prev != NULL && p->next != NULL){
                                //Remove middle player from pending_players
                                p->prev->next = p->next;
                                p->next->prev = p->prev;
                            }

                            //Add new player to playerlist
                            if(playerlist == NULL){
                                playerlist = p;
                                first_player = p;
                                current_player = p;
                                p->next = NULL;
                                p->prev = NULL;
                            }else{
                                p->next = playerlist;
                                playerlist->prev = p;
                                p->prev = NULL;
                                playerlist = p;
                            }

                            //Send out alerts
                            char alert[MAXMESSAGE];
                            sprintf(alert, "%s has joined the game.\r\n",p->name);
                            printf("%s has joined the game.\n",p->name);
                            broadcast(alert);

                            //Broadcast current gameboard to all clients
                            broadcast_gameboard();

                            //Broadcast to all players who's move it is
                            alert_move(current_player);   
                            flag_continue = 1;    
                        } 
                        //Reset variable
                        same_name_flag = 0;
                    } 
                }  
            }
        }

        //If certain operation needs to refresh listen_fds
        //Start while loop again without looping through playerlist
        if(flag_continue == 1){
            flag_continue = 0;
            continue;
        }

        //Players who are currently in the game
        for (struct player *p = playerlist; p; p = p->next) {
            if (FD_ISSET(p->fd, &listen_fds)) {

                //If it is NOT player's move
                if(p->fd != current_player->fd){
                    // Read what user says into a "garbage variable"
                    char storage[MAXMESSAGE];
                    int nbytes = read(p->fd, storage, MAXMESSAGE);
                    //If player has disconnected
                    if(nbytes == 0){
                        printf("%s has disconnected...\n", p->name);
                        
                        if(p->prev != NULL && p->next == NULL){
                            // First/oldest player who joined disconnects
                            p->prev->next = NULL;
                            first_player = p->prev;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        else if(p->prev == NULL && p->next != NULL){
                            // Last/newest player who joined disconnects
                            playerlist = p->next;
                            p->next->prev = NULL;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        else if(p->prev != NULL && p->next != NULL){
                            // Player who joined in the middle disconnects
                            p->prev->next = p->next;
                            p->next->prev = p->prev;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        FD_CLR(p->fd, &all_fds);
                        free(p);
                        break;
                    }
                    //If player did not disconnect
                    //Notify player that it is not their move
                    char message[MAXMESSAGE] = "It is not your move.\r\n";
                    write(p->fd, message, strlen(message)); 
                    storage[nbytes] = '\0';

                }
                //If it IS player's move
                else{
                    char user_input[MAXMESSAGE];
                    int nbytes = read(p->fd, user_input, MAXMESSAGE);

                    //If current player disconnected
                    if(nbytes == 0){
                        printf("%s has disconnected...\n", current_player->name);
                        // If the only player in the game disconnects
                        if(current_player->next == NULL && current_player->prev == NULL){
                            current_player = NULL;
                            playerlist = NULL;
                            first_player = NULL;                           
                        }
                        // First/oldest player who joined disconnects
                        else if(current_player->prev != NULL && current_player->next == NULL){
                            current_player = current_player->prev;
                            current_player->next = NULL;
                            if(current_player->prev == NULL){
                                playerlist = current_player;
                            }
                            
                            first_player = current_player;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        // Last/newest player who joined disconnects
                        else if(current_player->prev == NULL && current_player->next != NULL){
                            current_player = current_player->next;
                            current_player->prev = NULL;
                            playerlist = current_player;
                            current_player = first_player;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        // Player who joined in the middle disconnects
                        else if(current_player->prev != NULL && current_player->next != NULL){
                            current_player->prev->next = current_player->next;
                            current_player->next->prev = current_player->prev;
                            current_player = current_player->prev;
                            alert_disconnection(p->name);
                            broadcast_gameboard();
                            alert_move(current_player); 
                        }
                        FD_CLR(p->fd, &all_fds);
                        continue;
                    }   
                    //If player did not disconnect
                    else{
                        user_input[nbytes] = '\0';

                        //Get player input and make move
                        char *leftover;
                        int move_pit = strtol(user_input, &leftover, 10);

                        if(current_player->pits[move_pit] == 0 || move_pit < 0 || move_pit >= NPITS){
                            char invalid_pit_msg[] = "This is an invalid pit. Please pick another pit.\r\n";
                            write(current_player->fd, invalid_pit_msg, strlen(invalid_pit_msg));
                        }else{
                            make_move(move_pit, current_player);
                            broadcast_gameboard();   
                            // Broadcast to all players who's move it is
                            alert_move(current_player);              
                        }
                    }
                }
            }
        }
    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}

/*
 * Accepts new player and creates new player struct.
 * Initializes pits and fd.
 * Returns new player.
 */
struct player *accept_connection(int fd) {

    //Accept new player
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }

    //Create new player and set fd
    struct player *new_player = malloc(sizeof(struct player));
    new_player->fd = client_fd;   

    // Initialize the pits with beads
    int num_pebbles = compute_average_pebbles();
    for(int i = 0; i < NPITS; i++){
        new_player->pits[i] = num_pebbles;
    }
    new_player->pits[NPITS] = 0;
    new_player->after = 0;

    for(int i = 0; i < MAXNAME + 1; i++){
        new_player->name[i] = '\0';
    }

    for(int i = 0; i < MAXNAME + 1; i++){
        new_player->buf[i] = '\0';
    }

    return new_player;   
}

/*
 * Makes move for player p and updates gameboard.
 * Broadcasts the move that was made.
 * pit_num - the pit that the player p has selected.
 *       p - the current player making the move
 */
void make_move(int pit_num, struct player *p){
    int num_pebbles = p->pits[pit_num];
    struct player *current_p = p;
    int current_pit_num = pit_num;

    // Remove all beads from the selected pit
    p->pits[current_pit_num] = 0;

    // Distribute the beads
    while(num_pebbles > 0){
        if(current_pit_num >= NPITS){
            current_pit_num = 0;
            //Reached the head of linked list, loop back to first player
            if(current_p->prev == NULL){
                current_p = first_player;
            }else{
                current_p = current_p->prev;
            }
        }else{
            current_pit_num += 1;
        }
        // Do not distribute pebble in other player's end pit
        if(current_pit_num == NPITS && current_p != p){
            current_pit_num = 0; 
            if(current_p->prev == NULL){
                current_p = first_player;
            }else{
                current_p = current_p->prev;
            }
        }
        current_p->pits[current_pit_num] += 1;
        num_pebbles -= 1;
    }

    //Broadcast current player's move
    char alert_move_msg[MAXMESSAGE];
    sprintf(alert_move_msg, "--- %s moved pit %d ---\r\n", current_player->name, pit_num);
    printf("--- %s moved pit %d ---\n", current_player->name, pit_num);
    broadcast(alert_move_msg);
    

    // If ended in own end pit current player gets to go again
    if(current_p == p && current_pit_num == NPITS){
        current_player = p;
    }else{
        // Increment current player to next player
        if(current_player->prev == NULL){
            current_player = first_player;
        }else{
            current_player = current_player->prev;
        }
    }
}

/*
 * Display current gameboard state to all current players.
 */
void broadcast_gameboard(){
    for (struct player *p = playerlist; p; p = p->next) {
        get_player_display(p);
    }  
}

/*
 * Broadcasts player p's gameboard.
 */
void get_player_display(struct player *p){
    char player_display[MAXMESSAGE];
    int pit_arr[NPITS];

    for(int i = 0; i <= NPITS; i++){
        pit_arr[i] = p->pits[i];
    }

    snprintf(player_display, MAXMESSAGE, "%s:  ", p->name);
    for(int i = 0; i < NPITS; i++){
        char add[MAXMESSAGE];
        sprintf(add, "[%d]%d ", i, pit_arr[i]);
        strcat(player_display, add);
    }

    char add[MAXMESSAGE];
    sprintf(add, "[end pit]%d ", pit_arr[NPITS]);
    strcat(player_display, add);
    strcat(player_display, "\r\n");
    broadcast(player_display);
}

/*
 * Alert other players of a disconnection.
 */
void alert_disconnection(char *name){
    for (struct player *p = playerlist; p; p = p->next) {
        char alert_msg[MAXMESSAGE];
        sprintf(alert_msg, "%s has disconnected.\r\n", name);
        write(p->fd, alert_msg, strlen(alert_msg));
    }
}

/*
 * Alert the current player it is their move.
 * Alert all other players who's move it is.
 */
void alert_move(struct player *cur_player){
    for (struct player *p = playerlist; p; p = p->next) {
        if(cur_player != p){
            char alert_msg[MAXMESSAGE];
            sprintf(alert_msg, "It is %s's move.\r\n", cur_player->name);
            write(p->fd, alert_msg, strlen(alert_msg));
        }
    }
    char move_msg[] = "Your move?\r\n";
    write(current_player->fd, move_msg, strlen(move_msg));
}

/*
 * Checks to see if name has already been taken by other players.
 * Returns 1 if name is not unique. 
 * Returns 0 if name is unique.
 */
int is_unique_name(char *name){
    for (struct player *p = playerlist; p; p = p->next) {
        if(strcmp(p->name, name) == 0){
            return 1;
        }
    }
    return 0;
}

/*
 * Search the first n characters of buf for a network newline (\r\n).
 * Return the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_network_newline(const char *buf, int n) {
    for(int i = 1; i < n; i++){
        char temp = buf[i-1];
        char temp1 = buf[i];

        if(temp == '\r' || temp1 == '\n'){
            return (i);
        }
    }

/*    for(int i = 1; i < n; i++){
        if(buf[i] == '\n'){
            return (i);
        }
    }*/
    return -1;
}

/*
 * Broadcasts s to all current players.
 */
void broadcast(char *s){
    
    for (struct player *p = playerlist; p; p = p->next) {
        write(p->fd, s, strlen(s));
    }
}


void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}