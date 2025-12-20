#ifndef PARSER_H
#define PARSER_H
#include "board.h"

#define MAX_COMMAND_LENGTH 256
#define MAX_FILE_NUM 100    // maximo de ficheiros no programa
#define MAX_PATH 4096       // máximo de caracteres de um path

typedef struct {
    char level_files[MAX_FILE_NUM][MAX_PATH];     // array containing every path/filename of .lvl files
    char pacman_files[MAX_FILE_NUM][MAX_PATH];    // array containing every path/filename of .p files
    char ghost_files[MAX_FILE_NUM][MAX_PATH];     // array containing every path/filename of .m files
    int level_count;                              // number of .lvl files
    int pacman_count;                             // number of .p files
    int ghost_count;                              // number of .m files
} files_t;


int read_line(int fd, char* buffer);
int read_level(board_t* board, char* filename, char* dirname);
int read_pacman(board_t* board, int points);
int read_ghosts(board_t* board);
files_t manage_files(const char *path);

#endif
