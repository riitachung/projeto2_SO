#ifndef GAME_H
#define GAME_H

#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>

/* Headers do projeto */
#include "board.h"
#include "display.h"

/* --------- CONSTANTES DE CONTROLO DO JOGO --------- */

#define CONTINUE_PLAY 0
#define NEXT_LEVEL    1
#define QUIT_GAME     2
#define LOAD_BACKUP   3
#define CREATE_BACKUP 4

/* --------- ESTRUTURAS --------- */

typedef struct {
    board_t *board;
    int ghost_index;
} ghost_thread_arg_t;

/* --------- VARIÁVEIS GLOBAIS --------- */
/* Definida no .c */
extern int thread_shutdown;
extern board_t game_board;
extern int victory;
extern int game_over;
/* --------- FUNÇÕES EXPORTADAS --------- */

/* Backup */
int create_backup(void);

/* Atualização do ecrã */
void screen_refresh(board_t *game_board, int mode);

/* Threads */
void* ncurses_thread(void *arg);
void* pacman_thread(void *arg);
void* ghost_thread(void *arg);

void get_board(board_t *boardcpy);

/* Sessão principal do jogo */
int start_session(char *levels_dir);

#endif /* GAME_H */
