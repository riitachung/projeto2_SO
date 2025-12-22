#include "board.h"
#include "parser.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 10
#define MAX_CLIENTS 100

/*--------- ESTRUTURAS -----------*/

typedef struct SessionArguments {                  // estrutura que guarda argumentos da sessão
   int client_id;
   int client_index;
   int req_pipe;
   int notif_pipe;
   board_t board;
   char* levels_dir;
   files_t session_files;
   int current_level;
   int accumulated_points;
   int victory;
   int game_over;
   pthread_rwlock_t victory_lock;  
}SessionArguments;

typedef struct {                                   // argumentos para a thread do monstro
    struct SessionArguments *sessionArguments;
    int ghost_index;
} ghost_thread_arg_t;

typedef struct {                                   // argumentos para a thread do pacman
    board_t *board;
    int *req_pipe;
} pacman_thread_arg_t;

typedef struct {                                   // estrutura para guardar pedidos de sessão
   char req_pipe_path[40];
   char notif_pipe_path[40];
} session_request_t;

typedef struct {
   int id;
   int points;
   int active;
} client_game_t;


/*--------- VARIÁVEIS GLOBAIS DO BUFFER ----------*/
session_request_t buffer[MAX_BUFFER_SIZE];
client_game_t clients[MAX_CLIENTS];
pthread_mutex_t buffer_mutex;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t empty_buffer;                                // semáforo que conta espaços vazios no buffer
sem_t full_buffer;                                 // semáforo que conta pedidos disponíveis
int in = 0;                                        // indice de escrita (onde o produtor(host_thread) vai escrever/ colocar)
int out = 0;                                       // indice de leitura (onde o consumidor(session_thread) vai ler/ tirar)
volatile sig_atomic_t sigusr_received = 0;          // flag para sinal SIGUSR1


/*--------- FUNÇÃO AUXILIAR À ORDENAÇÃO POR PONTOS ----------*/
int sort_clients(const void *a, const void *b) {
   client_game_t *clientA = (client_game_t *)a;
   client_game_t *clientB = (client_game_t *)b;
   return clientB->points - clientA->points; // ordem decrescente
}

/*--------- FUNÇÃO QUE GERA O FICHEIRO TOP 5 ---------*/
void generate_file() {
   int top5_fd = open("top5_clients.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);   // cria, se existir apaga conteúdo antigo
   if(top5_fd < 0) return;

   client_game_t active_clients[MAX_CLIENTS];
   int active_count = 0;

   pthread_mutex_lock(&clients_mutex);
   for(int i = 0; i < MAX_CLIENTS; i++){
      if(clients[i].active) {
         active_clients[active_count++] = clients[i];
      }
   }
   pthread_mutex_unlock(&clients_mutex);
   qsort(active_clients, active_count, sizeof(client_game_t), sort_clients);
   for(int i = 0 ; i < active_count && i < 5; i++) {
      char client_buf[128];
      int len = snprintf(client_buf, sizeof(client_buf), "CLIENTE: %d PONTOS: %d\n", active_clients[i].id, active_clients[i].points);
      if(write(top5_fd, client_buf, len) != len) {
         break;
      }
   }
   close(top5_fd);
}


/*--------- THREAD DO PACMAN -----------*/
void* pacman_thread(void *arg) {
   struct SessionArguments *args = arg;           
   board_t *board = &args->board;
   int req_pipe = args->req_pipe;                  // pipe que recebe pedidos de movimentos do cliente
   
   pacman_t* pacman = &board->pacmans[0];
   
   while (1) {
      sleep_ms(board->tempo * (1 + pacman->passo));

      command_t* play;
      command_t c;

      // PACMAN MANUAL
      if (pacman->n_moves == 0) {
         char opcode;
         char command_client;

         if(read(req_pipe, &opcode, sizeof(char)) != 1) {            // Lê opcode de pedido de comando
            pthread_rwlock_wrlock(&args->victory_lock);
            args->game_over = 1;                                     // erro na leitura -> termina o jogo
            pthread_rwlock_unlock(&args->victory_lock);
            break;
         }
         if(opcode == 2) {                                           // pedido para disconnect
            pthread_rwlock_wrlock(&args->victory_lock);
            args->game_over = 1;
            pthread_rwlock_unlock(&args->victory_lock);
            break;
         }
         if(opcode != 3) {                                           // se não for pedido de comando recomeca while
            continue;
         }
         if(read(req_pipe, &command_client, sizeof(char)) != 1) { // se for pedido de comando lê o comando
            pthread_rwlock_wrlock(&args->victory_lock);
            args->game_over = 1;                                     // erro na leitura -> termina o jogo
            pthread_rwlock_unlock(&args->victory_lock);
            break;
         }             
         c.command = command_client;
         c.turns = 1;
         play = &c;
      }
      // PACMAN AUTOMÁTICO
      else {
         play = &pacman->moves[pacman->current_move%pacman->n_moves];
      }

      debug("Comando do pacman: %c\n", play->command);

      // QUIT
      if (play->command == 'Q') {
         pthread_rwlock_wrlock(&args->victory_lock);
         args->game_over = 1;
         pthread_rwlock_unlock(&args->victory_lock);
      }

      pthread_rwlock_wrlock(&board->state_lock);
      int result = move_pacman(board, 0, play);                         // move o pacman com a jogada
      debug("result do move pacman --> %d\n", result);
      
      // NEXT LEVEL
      if (result == REACHED_PORTAL) {
         debug("NEXT_LEVEL\n");
         pthread_rwlock_wrlock(&args->victory_lock);
         args->victory = 1;
         pthread_rwlock_unlock(&args->victory_lock);
         pthread_rwlock_unlock(&board->state_lock);
         break;
      }
      // PACMAN MORTO
      if(result == DEAD_PACMAN) {
         debug("DEAD_PACMAN -> QUIT_GAME\n");
         pthread_rwlock_wrlock(&args->victory_lock);
         args->game_over = 1;
         pthread_rwlock_unlock(&args->victory_lock);
         pthread_rwlock_unlock(&board->state_lock);
         break;
      }
      pthread_rwlock_unlock(&board->state_lock);
   }
   debug("Pacman thread terminada\n");
   return NULL;
}

/*---------- THREAD DO MONSTRO -----------*/
void* ghost_thread(void *arg) {
   debug("começou ghost thread\n");
   ghost_thread_arg_t *arg_ghost = arg;
   struct SessionArguments *args = arg_ghost->sessionArguments;
   board_t *board = &args->board;
   int ghost_ind = arg_ghost->ghost_index;
   int result;

   free(arg_ghost);
   ghost_t* ghost = &board->ghosts[ghost_ind];

   while (1) {
      sleep_ms(board->tempo);

      pthread_rwlock_rdlock(&args->victory_lock);
      int current_game_over = args->game_over;
      int current_victory = args->victory;
      int stop = current_game_over || current_victory;
      pthread_rwlock_unlock(&args->victory_lock);
      
      if(stop) break;
/*
      if(current_game_over || current_victory) {                        // termina a thread se o jogo terminou
         debug("game_over detetado -> acaba a ghost thread\n");
         pthread_exit(NULL);
      }*/

      pthread_rwlock_wrlock(&board->state_lock);
      if(ghost->n_moves > 0)  {                                          // move o monstro
         result =move_ghost(board, ghost_ind, &ghost->moves[ghost->current_move%ghost->n_moves]);
       // PACMAN MORTO
         if(result == DEAD_PACMAN) {
            debug("DEAD_PACMAN -> QUIT_GAME\n");
            pthread_rwlock_wrlock(&args->victory_lock);
            args->game_over = 1;
            pthread_rwlock_unlock(&args->victory_lock);
            pthread_rwlock_unlock(&board->state_lock);
            break;
         }
      }
      pthread_rwlock_unlock(&board->state_lock);
   }
   return NULL;
}

/*---------- THREAD GESTORA DE SESSÃO -----------*/
void* session_thread (void* arg) {
   char* levels_dir = (char*) arg;
   debug("começou session thread\n");

/*--------FASE GESTÃO DA SESSÃO ---------*/
   while(1){
      sem_wait(&full_buffer);                                           // leitura do buffer produtor-consumidor
      pthread_mutex_lock(&buffer_mutex);
      session_request_t request = buffer[out];           // request.req_pipe_path = "/tmp/id_request"
      out = (out + 1) % MAX_BUFFER_SIZE;
      pthread_mutex_unlock(&buffer_mutex);
      sem_post(&empty_buffer);

      int notif_fd, req_fd;
      char opcode = 1, result = 0;

      // OBTER O ID DO CLIENTE
      char* c = strrchr(request.req_pipe_path, '/');     // encontrar a última barra  de "/tmp/{id}_request" => c = /{id}_request
      c++;                                              // => c = {id}_request
      int client_id = atoi(c);                           // lê os números até encontrar algo que nãpo seja número
      
      notif_fd = open(request.notif_pipe_path, O_WRONLY);
      if(notif_fd < 0) continue;                     
      if(write(notif_fd, &opcode, sizeof(char)) != 1||                            // envia o resultado do connect
         write(notif_fd, &result, sizeof(char)) != 1) {
         close(notif_fd);
         continue;
      }
      req_fd = open(request.req_pipe_path, O_RDONLY);
      if(req_fd < 0) {
         close(notif_fd);
         continue;
      }

      // define argumentos da sessão
      struct SessionArguments *session = malloc(sizeof(SessionArguments));
      if(!session)   {
         debug("Erro ao alocar memória para sessão\n");
         close(notif_fd);
         close(req_fd);
         continue;
      }
      session->req_pipe = req_fd;
      session->notif_pipe = notif_fd;
      session->levels_dir = levels_dir;
      session->victory = 0;
      session->game_over = 0;
      session->client_id = client_id;
      int current_level = 0;
      int accumulated_points = 0;
      int threads_running = 1;
      pthread_rwlock_init(&session->victory_lock, NULL);

      // ADICIONA A INFO DE UM CLIENTE À LISTA DE CLIENTES
      pthread_mutex_lock(&clients_mutex);
      // Procurar uma posição livre no array
      int client_index = -1;
      for(int i = 0; i < MAX_CLIENTS; i++) {
         if(!clients[i].active) {
            client_index = i;
            break;
         }
      }

      if(client_index == -1){
         debug("Número máximo de clientes atingido\n");
         close(session->req_pipe);
         close(session->notif_pipe);
         pthread_mutex_unlock(&clients_mutex);
         free(session);
         continue;
      }
      clients[client_index].id = session->client_id;
      clients[client_index].points = 0;
      clients[client_index].active = 1;
      session->client_index = client_index;
      pthread_mutex_unlock(&clients_mutex);

      // LÊ DIRETORIA
      session->session_files = manage_files(levels_dir);
      files_t files = session->session_files;

      // DÁ LOAD AO PRIMEIRO NÍVEL
      load_level(&session->board, files.level_files[current_level], levels_dir, accumulated_points);
      debug("nível %s carregado\n", files.level_files[current_level]);

      // COMEÇA AS THREADS
      int n_ghosts = session->board.n_ghosts;
      pthread_t pacman_tid;
      pthread_t *ghost_tids = malloc(n_ghosts * sizeof(pthread_t));
      pthread_create(&pacman_tid, NULL, pacman_thread, (void*) session);
      for (int i = 0; i < n_ghosts; i++) {
         ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
         arg_ghost->sessionArguments = session;
         arg_ghost->ghost_index = i;
         pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
      }
      
      // LÊ E ENVIA PERIODICAMENTE O TABULEIRO
      board_t temp;
      while(1){
         pthread_rwlock_rdlock(&session->board.state_lock);
         temp = session->board;   

         // cópia das flags victory e game_over para evitar excesso de locks
         pthread_rwlock_rdlock(&session->victory_lock);
         int current_victory = session->victory;
         int current_game_over = session->game_over;
         pthread_rwlock_unlock(&session->victory_lock);
         

         // se o board ainda não foi inicializado no load_level, não escrever nada
         if (temp.width == 0 && temp.height == 0 && temp.board == NULL) {
            pthread_rwlock_unlock(&session->board.state_lock);
            debug("Board ainda não disponível\n");
            sleep_ms(10);
            continue; 
         }

         // ESCREVE NO PIPE DAS NOTIFICAÇÕES A INFORMAÇÃO PARA CONSTRUIR TABULEIRO
         char opcode = 4;
         if(write(notif_fd, &opcode, sizeof(char)) != 1 ||
         write(notif_fd, &temp.width, sizeof(int)) != sizeof(int) ||
         write(notif_fd, &temp.height, sizeof(int)) != sizeof(int) ||
         write(notif_fd, &temp.tempo, sizeof(int)) != sizeof(int) ||
         write(notif_fd, &current_victory, sizeof(int)) != sizeof(int) ||                            
         write(notif_fd, &current_game_over, sizeof(int)) != sizeof(int) ||                         
         write(notif_fd, &temp.pacmans[0].points, sizeof(int)) != sizeof(int)) {
            pthread_rwlock_unlock(&session->board.state_lock);  
            debug("Erro ao escrever no pipe de notificações (server)\n");
            break;
         }
         
         // ATUALIZAÇÃO PONTUACAO DO CLIENTE CONSTANTE
         pthread_mutex_lock(&clients_mutex);
         clients[session->client_index].points = temp.pacmans[0].points;
         pthread_mutex_unlock(&clients_mutex);
         
         char *data = malloc(temp.width * temp.height);
         if(!data){
            pthread_rwlock_unlock(&session->board.state_lock);  
            debug("Erro ao alocar memória para data do board\n");
            break;
         }

         for (int i = 0; i < temp.width * temp.height; i++) {
            if(temp.board[i].content == 'M'){
               data[i] = 'M';
            } else if (temp.board[i].content == 'W') {
               data[i] = '#';
            } else {
               if(temp.board[i].has_dot == 1)
                  data[i] = '.';
               else if (temp.board[i].has_portal == 1)
                  data[i] = '@';
               else data[i] = ' ';
            }
         }
         int x = temp.pacmans[0].pos_x;
         int y = temp.pacmans[0].pos_y;
         int idx = y * temp.width + x;
         if (idx >= 0 && idx < temp.width * temp.height) {
            data[idx] = 'C';
         }

         pthread_rwlock_unlock(&session->board.state_lock);  

         if(write(notif_fd, data, (temp.width * temp.height)) != (temp.width * temp.height)) {
            debug("Erro data\n");
            free(data);
            break;
         }
         free(data);
         
         // VERIFICA SE DEVE SAIR (APÓS ENVIAR O BOARD)
         if (current_game_over == 1) {
            debug("O jogo terminou completamente\n");
            break;
         }  

         // no caso de vitória (reached_portal)
         if(current_victory) {
            accumulated_points = session->board.pacmans[0].points;  // Guardar pontos
            current_level++;
            pthread_join(pacman_tid, NULL);
            for(int i = 0; i < n_ghosts; i++){
               pthread_join(ghost_tids[i], NULL);
            }
            free(ghost_tids);
            threads_running = 0;

            // se ainda houver mais levels para jogar
            if(current_level < files.level_count){
               // faz unload antes de carregar o proximo nível
               unload_level(&session->board);
               load_level(&session->board, files.level_files[current_level], levels_dir, accumulated_points);
               
               // reeinicia as flags
               pthread_rwlock_wrlock(&session->victory_lock);
               session->victory = 0;
               session->game_over = 0;
               pthread_rwlock_unlock(&session->victory_lock);

               // recomeça as threads
               n_ghosts = session->board.n_ghosts;
               ghost_tids = malloc(n_ghosts * sizeof(pthread_t));
               pthread_create(&pacman_tid, NULL, pacman_thread, (void*) session);
               for (int i = 0; i < n_ghosts; i++) {
                  ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
                  arg_ghost->sessionArguments = session;
                  arg_ghost->ghost_index = i;
                  pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
               }
            } else {
               // último nível completo
               pthread_rwlock_wrlock(&session->victory_lock);
               session->game_over = 1;
               pthread_rwlock_unlock(&session->victory_lock);
               debug("Último nível completo, game_over setado\n");
               // NÃO FAZER UNLOAD - ainda precisa do board para enviar o último frame
            }
         }
         sleep_ms(temp.tempo); 
      }
      
      // ESPERA PELAS THREADS
      if(threads_running){
         pthread_join(pacman_tid, NULL);
         for(int i = 0; i < n_ghosts; i++) {
            pthread_join(ghost_tids[i], NULL);
         }
         debug("threads terminadas\n");
         free(ghost_tids);
      }
      
      // FAZER UNLOAD DO ÚLTIMO NÍVEL
      unload_level(&session->board);
      debug("unload do último nível feito\n");
      close(session->req_pipe);
      close(session->notif_pipe);
      pthread_rwlock_destroy(&session->victory_lock);
      clients[session->client_index].active = 0;
      free(session);
   }
   return NULL;
}

/*--------- THREAD ANFITRIÃ ----------*/
void* host_thread (void* arg) {
   debug("começou host thread\n");

   sigset_t set;
   sigemptyset(&set);
   sigaddset(&set, SIGUSR1);
   pthread_sigmask(SIG_UNBLOCK, &set, NULL); // desbloqueia sinais sigusr1 nesta thread
   int server_fd = *(int*) arg; 
   char opcode, req_pipe_path[40], notif_pipe_path[40];
    
    // LÊ FIFO DE REGISTO CONSTANTEMENTE
    while(1){

      if(sigusr_received) {
         sigusr_received = 0;
         debug("Sinal recebido, gerando ficheiro\n");
         generate_file();
      }

      if(read(server_fd, &opcode, sizeof(char)) != 1) continue;      // se opcode <= 0 erro na leitura, recomeça o ciclo
      if(opcode != 1) continue;                                      // se não for pedido de inicio de sessao recomeça o ciclo
      // se opcode == 1, lê os nomes dos pipes associados ao cliente
      if(read(server_fd, req_pipe_path, sizeof(req_pipe_path)) != 40) continue;
      if(read(server_fd, notif_pipe_path, sizeof(notif_pipe_path)) != 40) continue;
      req_pipe_path[39] = '\0';
      notif_pipe_path[39] = '\0';
      session_request_t request;

      strcpy(request.req_pipe_path, req_pipe_path);
      strcpy(request.notif_pipe_path, notif_pipe_path);

      sem_wait(&empty_buffer);                              // espera q existam espacos no buffer, buffer cheio -> bloqueia
      pthread_mutex_lock(&buffer_mutex);                    // so uma thread pode aceder ao buffer de uma vez para evitar leituras e escritas incorretas

      buffer[in] = request;                                 // escreve pedido no buffer
      in = (in + 1) % MAX_BUFFER_SIZE;
      
      pthread_mutex_unlock(&buffer_mutex);
      sem_post(&full_buffer);       
   }
   return NULL;
}

void signal_handler(int sigusr) {
   (void)sigusr;
   sigusr_received = 1;
}


int main(int argc, char *argv[]) { // PacmanIST levels_dir max_games nome_do_FIFO_de_registo
   // verificar e receber argumentos
   if(argc != 4) return -1;
   char *levels_dir = argv[1];
   int max_games = atoi(argv[2]);
   const char *server_pipe_path = argv[3];

   int server_fd;
   unlink(server_pipe_path);
   open_debug_file("server-debug.log");
   
   signal(SIGPIPE, SIG_IGN);                                // evita erro SIGPIPE
   struct sigaction sa; 
   sa.sa_handler = signal_handler;
   sigemptyset(&sa.sa_mask); 
   sa.sa_flags = 0; 
   sigaction(SIGUSR1, &sa, NULL);

   pthread_mutex_init(&buffer_mutex, NULL);                 // inicia mutexs e semaforos
   sem_init(&empty_buffer, 0, MAX_BUFFER_SIZE);
   sem_init(&full_buffer, 0, 0);

   // CRIA E ABRE O FIFO DO SERVIDOR
   if(mkfifo(server_pipe_path, 0666) < 0) return -1;
   server_fd = open(server_pipe_path, O_RDWR);
   if(server_fd < 0) return -1;

   // BLOQUEIO DO SIGUSR PARA AS THREADS DO CLIENTE
   sigset_t set;              // declara o conjunto de sinais
   sigemptyset(&set);         // si
   sigaddset(&set, SIGUSR1); // adiciona o sinal SIGUSR1 ao conjunto
   pthread_sigmask(SIG_BLOCK, &set, NULL); // bloqueia sinais nas threads filhas

   // CRIA THREAD ANFITRIÃ E THREADS DE SESSÃO
   pthread_t tid_host;
   pthread_create(&tid_host, NULL, host_thread, &server_fd);
   
   pthread_t *session_tids = malloc(max_games * sizeof(pthread_t));
   for(int i = 0; i < max_games; i++){
      pthread_create(&session_tids[i], NULL, session_thread, levels_dir);
   }

   pthread_join(tid_host, NULL);
   for(int i = 0; i < max_games; i++) {
      pthread_join(session_tids[i], NULL);
   }
   
   //pthread_rwlock_destroy(&victory_lock);
   close_debug_file();

   return 0;
}
