#include "api.h"
#include "protocol.h"
#include "display.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_COMMAND_LENGTH 256

Board board = {0};
bool stop_execution = false;
int tempo;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


//tarefa que recebe atualizações do tabuleiro do servidor, verifica as condições e desenha o tabuleiro
static void *receiver_thread(void *arg) {
    (void)arg;
    debug("receiver_thread:\n");

    while (true) {
        board = receive_board_update();                             // recebe o tabuleiro atualizado via api

        pthread_mutex_lock(&mutex);
        tempo = board.tempo;
        pthread_mutex_unlock(&mutex);

        draw_board_client(board);                                   // desenha o tabuleiro com as informações recebidas
        refresh_screen();
        
        if (!board.data || board.game_over == 1){                   // se o tabuleiro não for válido ou o jogo terminou, sai da thread
            pthread_mutex_lock(&mutex);
            stop_execution = true;
            pthread_mutex_unlock(&mutex);
            break;
        }

        sleep_ms(tempo);
    }

    debug("Returning receiver thread...\n");
    return NULL;
}


int main(int argc, char *argv[]) {

    if (argc != 3 && argc != 4) {
        fprintf(stderr,
            "Usage: %s <client_id> <register_pipe> [commands_file]\n",
            argv[0]);
        return 1;
    }

    const char *client_id = argv[1];
    const char *register_pipe = argv[2];
    const char *commands_file = (argc == 4) ? argv[3] : NULL;


    FILE *cmd_fp = NULL;
    if (commands_file) {
        cmd_fp = fopen(commands_file, "r");
        if (!cmd_fp) {
            perror("Failed to open commands file");
            return 1;
        }
    }

    char req_pipe_path[MAX_PIPE_PATH_LENGTH];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH];

    snprintf(req_pipe_path, MAX_PIPE_PATH_LENGTH,
             "/tmp/%s_request", client_id);

    snprintf(notif_pipe_path, MAX_PIPE_PATH_LENGTH,
             "/tmp/%s_notification", client_id);

    open_debug_file("client-debug.log");
    debug("Cliente pede login\n");

    if (pacman_connect(req_pipe_path, notif_pipe_path, register_pipe) != 0) {
        perror("Failed to connect to server");
        return 1;
    }
    debug("Cliente conectado\n");

    pthread_t receiver_thread_id;
    pthread_create(&receiver_thread_id, NULL, receiver_thread, NULL);

    terminal_init();
    set_timeout(500);
    if(board.width > 0 && board.height > 0)
        draw_board_client(board);
    refresh_screen();

    char command;
    int ch;

    while (1) {

        if (cmd_fp) {

            pthread_mutex_lock(&mutex);
            if (stop_execution) {
                debug("Stop execution\n");
                pthread_mutex_unlock(&mutex);
                break;
            }       
            pthread_mutex_unlock(&mutex);

            // Input from file
            ch = fgetc(cmd_fp);

            if (ch == EOF) {
                // Restart at the start of the file
                rewind(cmd_fp);
                continue;
            }

            command = (char)ch;

            if (command == '\n' || command == '\r' || command == '\0')
                continue;

            command = toupper(command);

            
            // Wait for tempo, to not overflow pipe with requests
            pthread_mutex_lock(&mutex);
            int wait_for = tempo;
            pthread_mutex_unlock(&mutex);

            // Garantir um delay mínimo mesmo se tempo for 0
            if (wait_for <= 0){
                continue;
            } 

            // se o comando for T, esperar o tempo necessário
            
            if (command == 'T') {
                int moves_to_wait;
                fscanf(cmd_fp, " %d", &moves_to_wait);
                debug("moves_to_wait: %d\n", moves_to_wait);
                debug("tempo: %d\n", wait_for);
                debug("tempo * moves_to_wait: %d\n", wait_for * moves_to_wait);
                sleep_ms(wait_for * moves_to_wait);
            }

            sleep_ms(wait_for);
            pthread_mutex_lock(&mutex);
            if (stop_execution) {
                debug("Stop execution after sleep\n");
                pthread_mutex_unlock(&mutex);
                break;
            }
            pthread_mutex_unlock(&mutex);
            
        } else {
            // Interactive input
            command = get_input();
            
            pthread_mutex_lock(&mutex);
            if (stop_execution) {
                debug("Stop execution\n");
                pthread_mutex_unlock(&mutex);
                break;
            }       
            pthread_mutex_unlock(&mutex);

            command = toupper(command);
        }

        if (command == '\0')
            continue;

        if (command == 'Q') {
            //pacman_play('Q');
            debug("Client pressed 'Q', quitting game\n");
            //break;
        }

        debug("Command: %c\n", command);

        pacman_play(command);

    }
    
    pthread_join(receiver_thread_id, NULL);
    debug("Receiver thread terminada\n");

    pacman_disconnect();
    debug("Pacman desconectado\n");
    if (cmd_fp)
        fclose(cmd_fp);

    pthread_mutex_destroy(&mutex);
    refresh_screen();
    terminal_cleanup();

    return 0;
}
