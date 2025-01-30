#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define ALIVE 1
#define DEAD  0

// Struktura do przekazania danych do wątku
typedef struct {
    int row;
    int col;
    int rows;
    int cols;
    int generations;
} CellData;

// Tablice globalne (dla uproszczenia przykładu)
static int **current_generation; // aktualna generacja
static int **next_generation;    // kolejna generacja (bufor do obliczeń)

// Semafory globalne
sem_t sem_done;   // sygnalizuje głównemu wątkowi, że dany wątek skończył obliczenia
sem_t sem_start;  // sygnalizuje wątkom, że mogą rozpocząć obliczenia kolejnej generacji

// Liczba wątków, które już zakończyły obliczenia w danej generacji
// (można też liczyć przez semafory, ale tu używamy licznika + semafor)
int done_count = 0;

// Mutex do ochrony zmiennych globalnych (np. done_count)
pthread_mutex_t done_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// Zmienna globalna używana przez wątki do sprawdzania, którą generację liczymy
int current_gen = 0;
int total_generations = 0;

// Liczba wszystkich komórek (wątków)
int num_threads = 0;

// Funkcja pomocnicza licząca liczbę żywych sąsiadów
int count_alive_neighbors(int row, int col, int rows, int cols) {
    int alive_neighbors = 0;
    // Przechodzimy po 8 sąsiadach (lub mniej na krawędziach)
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            // Pomijamy sprawdzanie komórki (0,0) względem (dr,dc) == (0,0)
            if (dr == 0 && dc == 0) continue;

            int r = row + dr;
            int c = col + dc;

            // Sprawdzenie zakresu
            if (r >= 0 && r < rows && c >= 0 && c < cols) {
                if (current_generation[r][c] == ALIVE) {
                    alive_neighbors++;
                }
            }
        }
    }
    return alive_neighbors;
}

// Funkcja wątku (każdy wątek to jedna komórka)
void* cell_thread(void *arg) {
    CellData *data = (CellData*)arg;
    int row = data->row;
    int col = data->col;
    int rows = data->rows;
    int cols = data->cols;
    int max_gen = data->generations;

    for (;;) {
        // 1. Czekamy na sygnał od głównego wątku, że rozpoczynamy liczenie tej generacji
        sem_wait(&sem_start);

        // Sprawdzamy, czy nie osiągnęliśmy już max. liczby generacji
        pthread_mutex_lock(&done_count_mutex);
        if (current_gen >= max_gen) {
            // Zwalniamy mutex i kończymy wątek
            pthread_mutex_unlock(&done_count_mutex);
            break;
        }
        pthread_mutex_unlock(&done_count_mutex);

        // 2. Obliczamy stan tej komórki w kolejnej generacji
        int alive_neighbors = count_alive_neighbors(row, col, rows, cols);
        int current_state = current_generation[row][col];
        int next_state = current_state;

        // Zasady Gry w życie:
        // 1. Żywa komórka z <2 żywymi sąsiadami umiera (samotność)
        // 2. Żywa komórka z >3 żywymi sąsiadami umiera (przeludnienie)
        // 3. Żywa komórka z 2 lub 3 żywymi sąsiadami żyje dalej
        // 4. Martwa komórka z 3 żywymi sąsiadami ożywa

        if (current_state == ALIVE) {
            if (alive_neighbors < 2 || alive_neighbors > 3) {
                next_state = DEAD;
            }
            // jeśli alive_neighbors == 2 lub 3, stan się nie zmienia (pozostaje ALIVE)
        } else {
            if (alive_neighbors == 3) {
                next_state = ALIVE;
            }
        }

        next_generation[row][col] = next_state;

        // 3. Zgłaszamy głównemu wątkowi, że ten wątek skończył
        pthread_mutex_lock(&done_count_mutex);
        done_count++;
        // Odblokowujemy sem_done, aby główny wątek mógł liczyć licznik
        sem_post(&sem_done);
        pthread_mutex_unlock(&done_count_mutex);
    }

    // Zwolnienie struktury arg w przypadku, gdy alokowaliśmy dynamicznie
    free(data);
    return NULL;
}

// Funkcja wyświetlająca planszę w konsoli (prosta wizualizacja ASCII)
void print_board(int **board, int rows, int cols) {
    // Można użyć np. system("clear") na Linux/Mac lub system("cls") na Windows, aby "odświeżać" ekran.
    // Tu dla przykładu tylko wypiszemy bez czyszczenia ekranu.
    printf("\n");
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (board[r][c] == ALIVE) {
                printf("O");
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
    printf("\n");
}

// Funkcja zapisująca planszę do pliku
void write_board_to_file(FILE *fp, int **board, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            // Zapiszmy jako 0/1
            if (board[r][c] == ALIVE) {
                fputc('0', fp);
            } else {
                fputc('.', fp);
            }
        }
        fputc('\n', fp);
    }
    fputc('\n', fp); // Odstęp między generacjami
}

// Funkcja główna
int main(int argc, char *argv[]) {

    if (argc < 6) {
        fprintf(stderr, "Użycie: %s <plik_wejsciowy> <plik_wyjsciowy> <rows> <cols> <generations>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *input_filename = argv[1];
    char *output_filename = argv[2];
    int rows = atoi(argv[3]);
    int cols = atoi(argv[4]);
    int generations = atoi(argv[5]);

    total_generations = generations;

    // Alokacja tablic 2D
    current_generation = malloc(rows * sizeof(int*));
    next_generation = malloc(rows * sizeof(int*));
    for (int i = 0; i < rows; i++) {
        current_generation[i] = malloc(cols * sizeof(int));
        next_generation[i]   = malloc(cols * sizeof(int));
    }

    // Wczytanie planszy z pliku
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
            if (ch == '1') {
                current_generation[r][c] = ALIVE;
            } else {
                current_generation[r][c] = DEAD;
            }
        }
        // Odczyt ew. znaku nowej linii
        fgetc(fin);
    }

    fclose(fin);

    // Inicjalizacja semaforów
    // sem_done – na starcie 0, bo żaden wątek nie jest jeszcze "done"
    sem_init(&sem_done, 0, 0);
    // sem_start – na starcie 0, wątki czekają aż główny wątek da im sygnał
    sem_init(&sem_start, 0, 0);

    // Utworzenie wątków
    pthread_t *threads = malloc(rows * cols * sizeof(pthread_t));
    num_threads = rows * cols;

    int idx = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            CellData *cd = malloc(sizeof(CellData));
            cd->row = r;
            cd->col = c;
            cd->rows = rows;
            cd->cols = cols;
            cd->generations = generations;

            pthread_create(&threads[idx], NULL, cell_thread, cd);
            idx++;
        }
    }

    // Otwieramy plik wyjściowy (np. do zapisu kolejnych generacji)
    FILE *fout = fopen(output_filename, "w");
    if (!fout) {
        perror("Nie można otworzyć pliku wyjściowego");
        exit(EXIT_FAILURE);
    }

    // Główna pętla symulacji
    for (int g = 0; g < generations; g++) {
        // 1. Dajemy sygnał każdemu wątkowi, że może obliczyć swój stan w generacji g
        for (int i = 0; i < num_threads; i++) {
            sem_post(&sem_start);
        }

        // 2. Czekamy aż wszystkie wątki (num_threads) skończą obliczenia tej generacji
        for (int i = 0; i < num_threads; i++) {
            sem_wait(&sem_done);
        }

        // W tym momencie next_generation[] zawiera obliczoną generację (g+1).
        // Przepisujemy ją do current_generation, aby wątki liczyły odświeżony stan.
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                current_generation[r][c] = next_generation[r][c];
            }
        }

        // Wyświetlamy w konsoli (opcjonalnie można czyścić ekran) i zapisujemy do pliku
        printf("Generacja %d:\n", g+1);
        print_board(current_generation, rows, cols);

        fprintf(fout, "Generacja %d:\n", g+1);
        write_board_to_file(fout, current_generation, rows, cols);

        // Zwiększamy licznik generacji globalny (dla wątków)
        pthread_mutex_lock(&done_count_mutex);
        current_gen++;
        done_count = 0; // reset licznika wątków "done"
        pthread_mutex_unlock(&done_count_mutex);

        // Mała pauza, aby wizualizacja nie "pędziła" zbyt szybko
        // (opcjonalnie)
        usleep(200000); // 0.2 sek
    }

    // Sygnał dla wątków, że kończymy pracę (current_gen >= generations)
    // Musimy "postować" sem_start wystarczająco wiele razy, aby każdy wątek mógł się obudzić i zakończyć
    pthread_mutex_lock(&done_count_mutex);
    current_gen = generations; // aby wątki przy następnym sem_wait sprawdziły warunek i wyszły
    pthread_mutex_unlock(&done_count_mutex);

    for (int i = 0; i < num_threads; i++) {
        sem_post(&sem_start);
    }

    // Czekamy, aż wątki się zakończą
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    fclose(fout);

    // Zwolnienie pamięci
    free(threads);
    for (int i = 0; i < rows; i++) {
        free(current_generation[i]);
        free(next_generation[i]);
    }
    free(current_generation);
    free(next_generation);

    sem_destroy(&sem_done);
    sem_destroy(&sem_start);

    pthread_mutex_destroy(&done_count_mutex);

    printf("Symulacja zakończona. Wynik zapisano do pliku: %s\n", output_filename);

    return 0;
}
