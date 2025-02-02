#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define ALIVE 1
#define DEAD  0

// Globalne zmienne: wymiary planszy i liczba generacji
int rows, cols, generations;

// Tablice 2D reprezentujące stan planszy
int **current_generation;
int **next_generation;

// Liczba wątków (pula wątków)
int num_workers;

// Zmienne synchronizacyjne – mutex i zmienne warunkowe
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_start = PTHREAD_COND_INITIALIZER; // do powiadamiania wątków o rozpoczęciu generacji
pthread_cond_t cond_done  = PTHREAD_COND_INITIALIZER; // do oczekiwania, aż wszyscy wątki zakończą obliczenia

// Globalne zmienne sterujące symulacją
int current_gen = 0; // numer bieżącej generacji (zadania)
int done_count = 0;  // licznik wątków, które ukończyły przetwarzanie aktualnej generacji

// Struktura przekazywana do wątku – określa przydział wierszy
typedef struct {
    int thread_id;
    int start_row;  // pierwszy przetwarzany wiersz (włącznie)
    int end_row;    // ostatni przetwarzany wiersz (wyłącznie)
} WorkerData;

// Funkcja pomocnicza: liczy liczbę żywych sąsiadów komórki (r, c)
int count_alive_neighbors(int r, int c) {
    int count = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0)
                continue;
            int nr = r + dr;
            int nc = c + dc;
            if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                if (current_generation[nr][nc] == ALIVE)
                    count++;
            }
        }
    }
    return count;
}

// Funkcja wątku-robotnika: każdy wątek przetwarza przypisany fragment planszy
void* worker_thread(void *arg) {
    WorkerData *data = (WorkerData*) arg;
    int local_gen = 0; // numer ostatniej przetworzonej generacji przez ten wątek

    while (1) {
        // Czekamy na sygnał rozpoczęcia nowej generacji
        pthread_mutex_lock(&cond_mutex);
        while (current_gen == local_gen) {
            pthread_cond_wait(&cond_start, &cond_mutex);
        }
        // Jeżeli numer generacji przekroczył liczbę symulowanych – kończymy pracę
        if (current_gen > generations) {
            pthread_mutex_unlock(&cond_mutex);
            break;
        }
        // Aktualizujemy lokalny numer generacji
        local_gen = current_gen;
        pthread_mutex_unlock(&cond_mutex);

        // Przetwarzamy przypisane wiersze dla bieżącej generacji
        for (int r = data->start_row; r < data->end_row; r++) {
            for (int c = 0; c < cols; c++) {
                int alive_neighbors = count_alive_neighbors(r, c);
                int current_state = current_generation[r][c];
                int next_state = current_state;

                // Zasady Gry w Życie:
                if (current_state == ALIVE) {
                    if (alive_neighbors < 2 || alive_neighbors > 3)
                        next_state = DEAD;
                } else {
                    if (alive_neighbors == 3)
                        next_state = ALIVE;
                }
                next_generation[r][c] = next_state;
            }
        }

        // Po zakończeniu obliczeń robotnik zgłasza ukończenie zadania:
        pthread_mutex_lock(&cond_mutex);
        done_count++;
        if (done_count == num_workers) {
            // Jeśli to był ostatni wątek – powiadamiamy główny wątek
            pthread_cond_signal(&cond_done);
        }
        pthread_mutex_unlock(&cond_mutex);
    }

    free(data);
    return NULL;
}

// Funkcja wyświetlająca planszę w konsoli (ASCII)
void print_board(int **board, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            printf("%c", board[r][c] == ALIVE ? 'O' : '.');
        }
        printf("\n");
    }
    printf("\n");
}

// Funkcja zapisująca planszę do pliku
void write_board_to_file(FILE *fp, int **board, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            fputc(board[r][c] == ALIVE ? '1' : '.', fp);
        }
        fputc('\n', fp);
    }
    fputc('\n', fp);
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Użycie: %s <plik_wejsciowy> <plik_wyjsciowy> <rows> <cols> <generations>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *input_filename  = argv[1];
    char *output_filename = argv[2];
    rows        = atoi(argv[3]);
    cols        = atoi(argv[4]);
    generations = atoi(argv[5]);

    // Alokacja pamięci dla planszy (tablice 2D)
    current_generation = malloc(rows * sizeof(int*));
    next_generation    = malloc(rows * sizeof(int*));
    for (int i = 0; i < rows; i++) {
        current_generation[i] = malloc(cols * sizeof(int));
        next_generation[i]    = malloc(cols * sizeof(int));
    }

    // Wczytanie początkowej planszy z pliku
    FILE *fin = fopen(input_filename, "r");
    if (!fin) {
        perror("Nie można otworzyć pliku wejściowego");
        exit(EXIT_FAILURE);
    }
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int ch = fgetc(fin);
            if (ch == EOF) {
                fprintf(stderr, "Zbyt mało danych w pliku wejściowym!\n");
                exit(EXIT_FAILURE);
            }
            current_generation[r][c] = (ch == '1') ? ALIVE : DEAD;
        }
        // Odczytujemy znak nowej linii (jeśli występuje)
        fgetc(fin);
    }
    fclose(fin);

    // Otwarcie pliku wyjściowego
    FILE *fout = fopen(output_filename, "w");
    if (!fout) {
        perror("Nie można otworzyć pliku wyjściowego");
        exit(EXIT_FAILURE);
    }

    // Wyświetlenie i zapisanie planszy początkowej
    printf("Generacja 0:\n");
    print_board(current_generation, rows, cols);
    fprintf(fout, "Generacja 0:\n");
    write_board_to_file(fout, current_generation, rows, cols);

    // Ustalamy liczbę wątków – możemy użyć liczby rdzeni systemowych
    num_workers = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_workers < 1)
        num_workers = 1;
    if (num_workers > rows)
        num_workers = rows; // nie tworzymy więcej wątków niż wierszy

    pthread_t *threads = malloc(num_workers * sizeof(pthread_t));

    // Dzielimy planszę – każdemu wątkowi przydzielamy pewien przedział wierszy
    int rows_per_worker = rows / num_workers;
    int remainder = rows % num_workers;
    int start = 0;
    for (int i = 0; i < num_workers; i++) {
        WorkerData *wd = malloc(sizeof(WorkerData));
        wd->thread_id = i;
        wd->start_row = start;
        int extra = (i < remainder) ? 1 : 0;
        wd->end_row = start + rows_per_worker + extra;
        start = wd->end_row;
        pthread_create(&threads[i], NULL, worker_thread, wd);
    }

    // Główna pętla symulacji – przetwarzamy kolejne generacje
    for (int gen = 1; gen <= generations; gen++) {
        // Rozpoczynamy nową generację: aktualizujemy globalną zmienną i powiadamiamy wątki
        pthread_mutex_lock(&cond_mutex);
        current_gen = gen;
        pthread_cond_broadcast(&cond_start);
        // Czekamy, aż wszystkie wątki zakończą obliczenia
        while (done_count < num_workers) {
            pthread_cond_wait(&cond_done, &cond_mutex);
        }
        // Resetujemy licznik ukończonych wątków dla kolejnej generacji
        done_count = 0;
        pthread_mutex_unlock(&cond_mutex);

        // Po zakończeniu obliczeń kopiujemy bufor next_generation do current_generation
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                current_generation[r][c] = next_generation[r][c];
            }
        }

        // Wyświetlamy oraz zapisujemy stan planszy
        printf("Generacja %d:\n", gen);
        print_board(current_generation, rows, cols);
        fprintf(fout, "Generacja %d:\n", gen);
        write_board_to_file(fout, current_generation, rows, cols);

        usleep(200000); // opcjonalna pauza 0.2 sekundy
    }

    // Sygnalizujemy wątkom zakończenie pracy (numer generacji przekroczony)
    pthread_mutex_lock(&cond_mutex);
    current_gen = generations + 1;
    pthread_cond_broadcast(&cond_start);
    pthread_mutex_unlock(&cond_mutex);

    // Czekamy na zakończenie wątków
    for (int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }

    fclose(fout);

    // Zwolnienie pamięci
    for (int i = 0; i < rows; i++) {
        free(current_generation[i]);
        free(next_generation[i]);
    }
    free(current_generation);
    free(next_generation);
    free(threads);

    // Niszczenie mutexa i zmiennych warunkowych
    pthread_mutex_destroy(&cond_mutex);
    pthread_cond_destroy(&cond_start);
    pthread_cond_destroy(&cond_done);

    printf("Symulacja zakończona. Wynik zapisano do pliku: %s\n", output_filename);
    return 0;
}
