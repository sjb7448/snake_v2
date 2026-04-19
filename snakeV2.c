#include <stdlib.h>
#include <curses.h>
#include <signal.h>
#include <sys/select.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>

#define DESIRED_WIDTH  70
#define DESIRED_HEIGHT 25

WINDOW * g_mainwin;
int g_oldcur, g_score = 0, g_width, g_height;
typedef struct {
    int x;
    int y;
} pos;
pos fruit;
pos enemy_head;

// 2D array of all spaces on the board.
bool *spaces;

// --------------------------------------------------------------------------
// Queue stuff (Refactored to support multiple queues for multiple snakes)

// Queue implemented as a doubly linked list
typedef struct s_node {
    pos *position; 
    struct s_node *prev;
    struct s_node *next;
} node;

typedef struct {
    node *front;
    node *back;
} queue;

queue player_q = {NULL, NULL};
queue enemy_q = {NULL, NULL};

// Returns the position at the front w/o dequeing
pos* peek( queue *q ) {
    return q->front == NULL ? NULL : q->front->position;
}

// Returns the position at the front and dequeues (Fixed memory leak from original)
pos dequeue( queue *q ) {
    pos p = {-1, -1};
    if( q->front == NULL ) return p;
    
    node *oldfront = q->front;
    q->front = q->front->next;
    
    if( q->front == NULL )
        q->back = NULL;
    else
        q->front->prev = NULL;

    p.x = oldfront->position->x;
    p.y = oldfront->position->y;
    
    free( oldfront->position );
    free( oldfront );
    return p;
}

// Queues a position at the back
void enqueue( queue *q, pos position ) {
    pos *newpos   = (pos*)  malloc( sizeof( pos ) ); 
    node *newnode = (node*) malloc( sizeof( node ) );

    newpos->x = position.x;
    newpos->y = position.y;
    newnode->position = newpos;
    newnode->next = NULL;
    newnode->prev = q->back;

    if( q->front == NULL && q->back == NULL ) {
        q->front = q->back = newnode;
    } else {
        q->back->next = newnode;
        q->back = newnode;
    }
}

void free_queue( queue *q ) {
    while( q->front ) {
        node *n = q->front;
        q->front = q->front->next;
        free( n->position );
        free( n );
    }
}
// --------------------------------------------------------------------------
// End Queue stuff

// Writes text to a coordinate
void snake_write_text( int y, int x, char* str ) {
    mvwaddstr( g_mainwin, y , x, str );
}

// Draws the borders
void snake_draw_board( ) {
    int i;
    for( i=0; i<g_height; i++ ) {
        snake_write_text( i, 0,         "X" );
        snake_write_text( i, g_width-1, "X" );
    }
    for( i=1; i<g_width-1; i++ ) {
        snake_write_text( 0, i,          "X" );
        snake_write_text( g_height-1, i, "X" );
    }
    snake_write_text( g_height+1, 2, "Score:" );
}

// Resets the terminal window and clears up the mem
void snake_game_over( ) {
    free( spaces );
    free_queue( &player_q );
    free_queue( &enemy_q );
    endwin();
    exit(0);
}

// Is the current position in bounds?
bool snake_in_bounds( pos position ) {
    return position.y < g_height - 1 && position.y > 0 && position.x < g_width - 1 && position.x > 0;
}

// Maps the x,y coordinates to an index in the array.
int snake_cooridinate_to_index( pos position ) {
    return g_width * position.y + position.x;
}

// Maps an index back to a position
pos snake_index_to_coordinate( int index ) {
    int x = index % g_width;
    int y = index / g_width;
    return (pos) { x, y };
}

// Draw the fruit somewhere randomly on the board
void snake_draw_fruit( ) {
    attrset( COLOR_PAIR( 3 ) );
    int idx;
    do {
        idx = rand( ) % ( g_width * g_height );
        fruit = snake_index_to_coordinate( idx );
    } while( spaces[idx] || !snake_in_bounds( fruit ) );    
    snake_write_text( fruit.y, fruit.x, "F" );
}

// LEVEL 2: Moves the fruit randomly
void snake_move_fruit() {
    int dir = rand() % 4; // 0: up, 1: down, 2: left, 3: right
    pos next_fruit = fruit;
    
    if (dir == 0) next_fruit.y--;
    else if (dir == 1) next_fruit.y++;
    else if (dir == 2) next_fruit.x--;
    else if (dir == 3) next_fruit.x++;

    // Only move if the spot is inside bounds and unoccupied
    if (snake_in_bounds(next_fruit) && !spaces[snake_cooridinate_to_index(next_fruit)]) {
        snake_write_text(fruit.y, fruit.x, " "); // Clear old fruit
        fruit = next_fruit;
        attrset(COLOR_PAIR(3));
        snake_write_text(fruit.y, fruit.x, "F"); // Draw new fruit
    }
}

// LEVEL 3: Enemy snake logic
void snake_move_enemy() {
    pos next_head = enemy_head;
    
    // AI: Move toward the fruit
    if (fruit.x > enemy_head.x) next_head.x++;
    else if (fruit.x < enemy_head.x) next_head.x--;
    else if (fruit.y > enemy_head.y) next_head.y++;
    else if (fruit.y < enemy_head.y) next_head.y--;

    // If primary move is blocked, try any open adjacent spot to avoid getting stuck
    int idx = snake_cooridinate_to_index(next_head);
    if (!snake_in_bounds(next_head) || spaces[idx]) {
        pos options[4] = {
            {enemy_head.x+1, enemy_head.y}, {enemy_head.x-1, enemy_head.y},
            {enemy_head.x, enemy_head.y+1}, {enemy_head.x, enemy_head.y-1}
        };
        bool found = false;
        for(int i=0; i<4; i++) {
            if (snake_in_bounds(options[i]) && !spaces[snake_cooridinate_to_index(options[i])]) {
                next_head = options[i];
                found = true;
                break;
            }
        }
        if (!found) return; // Completely blocked, skip turn
    }

    enemy_head = next_head;

    // Check Loss Condition: Enemy eats the food
    if (enemy_head.x == fruit.x && enemy_head.y == fruit.y) {
        snake_game_over();
    }
    
    idx = snake_cooridinate_to_index(enemy_head);
    // Check Loss Condition: Enemy hits the player
    if (spaces[idx]) {
        snake_game_over();
    }
    
    // Update enemy body
    spaces[idx] = true;
    enqueue(&enemy_q, enemy_head);
    
    // Fixed length of 3: Dequeue tail
    pos tail = dequeue(&enemy_q);
    spaces[snake_cooridinate_to_index(tail)] = false;
    snake_write_text(tail.y, tail.x, " ");
    
    attrset(COLOR_PAIR(6)); // Draw enemy in Magenta
    snake_write_text(enemy_head.y, enemy_head.x, "E");
}

// Handles moving the snake for each iteration
bool snake_move_player( pos head ) {
    attrset( COLOR_PAIR( 1 ) ) ;
    
    // Check if we ran into ourself or the enemy
    int idx = snake_cooridinate_to_index( head );
    if( spaces[idx] )
        snake_game_over( );
        
    spaces[idx] = true; // Mark the space as occupied
    enqueue( &player_q, head );
    g_score += 10;
    
    // Check if we're eating the fruit
    if( head.x == fruit.x && head.y == fruit.y ) {
        snake_draw_fruit( );
        g_score += 1000;
    } else {
        // Handle the tail
        pos tail = dequeue( &player_q );
        spaces[snake_cooridinate_to_index( tail )] = false;
        snake_write_text( tail.y, tail.x, " " );
    }
    
    // Draw the new head 
    snake_write_text( head.y, head.x, "S" );
    
    // Update scoreboard
    char buffer[25];
    sprintf( buffer, "%d", g_score );
    attrset( COLOR_PAIR( 2 ) );
    snake_write_text( g_height+1, 9, buffer );
    return true;
}

int main( int argc, char *argv[] ) {
    int key = KEY_RIGHT;
    if( ( g_mainwin = initscr() ) == NULL ) {
        perror( "error initialising ncurses" );
        exit( EXIT_FAILURE );
    }
    
    // Set up
    srand( time( NULL ) );
    noecho( );
    curs_set( 2 );
    halfdelay( 1 );
    keypad( g_mainwin, TRUE );
    g_oldcur = curs_set( 0 );
    start_color( );
    init_pair( 1, COLOR_RED,     COLOR_BLACK );
    init_pair( 2, COLOR_GREEN,   COLOR_BLACK );
    init_pair( 3, COLOR_YELLOW,  COLOR_BLACK );
    init_pair( 4, COLOR_BLUE,    COLOR_BLACK );
    init_pair( 5, COLOR_CYAN,    COLOR_BLACK );
    init_pair( 6, COLOR_MAGENTA, COLOR_BLACK );
    init_pair( 7, COLOR_WHITE,   COLOR_BLACK );
    getmaxyx( g_mainwin, g_height, g_width );
    
    g_width  = g_width  < DESIRED_WIDTH  ? g_width  : DESIRED_WIDTH;
    g_height = g_height < DESIRED_HEIGHT ? g_height : DESIRED_HEIGHT; 
    
    // Set up the 2D array of all spaces
    spaces = (bool*) malloc( sizeof( bool ) * g_height * g_width );
    for(int i = 0; i < g_height * g_width; i++) spaces[i] = false; // Initialize to avoid garbage data

    snake_draw_board( );
    snake_draw_fruit( );
    
    // Init Player Snake
    pos head = { 5,5 };
    enqueue( &player_q, head );
    
    // Init Enemy Snake (Level-3)
    enemy_head = (pos){ g_width - 5, g_height - 5 };
    for(int i=0; i<3; i++) {
        pos body_part = {enemy_head.x - 2 + i, enemy_head.y};
        enqueue(&enemy_q, body_part);
        spaces[snake_cooridinate_to_index(body_part)] = true;
        attrset(COLOR_PAIR(6));
        snake_write_text(body_part.y, body_part.x, "E");
    }
    
    int tick = 0; // Tracks game loops to control speeds
    
    // Event loop
    while( 1 ) {
        tick++;
        int in = getch( );
        if( in != ERR )
            key = in;
            
        switch( key ) {
            case KEY_DOWN: case 'j': case 'J': case 's': case 'S':
                head.y++; break;
            case KEY_RIGHT: case 'l': case 'L': case 'd': case 'D':
                head.x++; break;
            case KEY_UP: case 'k': case 'K': case 'w': case 'W':
                head.y--; break;
            case KEY_LEFT: case 'h': case 'H': case 'a': case 'A':
                head.x--; break;
        }
        
        // Player moves every loop
        if( !snake_in_bounds( head ) )    
            snake_game_over( );
        else
            snake_move_player( head );
            
        // Level 2 & 3: Moving food and enemy (they move at half speed)
        if (tick % 2 == 0) {
            snake_move_fruit();
            snake_move_enemy();
        }
    }
    snake_game_over( );
}
