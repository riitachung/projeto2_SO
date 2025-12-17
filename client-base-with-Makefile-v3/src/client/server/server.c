// criar fifo serveer
// criar a thread anfitria - abre fifo registo, le pedidos connect
// criar thread de sessao (max_games nª threads), espera pedido no buffer, abre pipes cliente
// envia connect ok, cri tabuleiro, entra no ciclo de ler opcodes e lidar com eles


int main(int argc, char *argv[]) { // PacmanIST levels_dir max_games nome_do_FIFO_de_registo
    if(argc != 4) return -1;
    char *levels_dir = argv[1];
    int max_games = argv[2];
    const char *server_pipe_path = argv[3];

    unlink(server_pipe_path);
    if(mkfifo(server_pipe_path, 0666) < 0) return -1;
}
