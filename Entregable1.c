#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define SYNC_READY  1
#define SYNC_DONE   2

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo_entrada> <archivo_salida>\n", argv[0]);
        return 1;
    }

    char *infile = argv[1];
    char *outfile = argv[2];

    /* ---------------------------------------------------------
       1) Abrir archivo de entrada y obtener tamaño
       --------------------------------------------------------- */
    int fd_in = open(infile, O_RDONLY);
    if (fd_in < 0) { perror("open entrada"); return 1; }

    struct stat st;
    if (fstat(fd_in, &st) < 0) { perror("fstat"); return 1; }
    size_t size_in = st.st_size;

    /* Leer archivo de entrada en buffer temporal */
    char *input_buf = malloc(size_in);
    if (!input_buf) { perror("malloc"); return 1; }

    if (read(fd_in, input_buf, size_in) != size_in) {
        perror("read");
        return 1;
    }
    close(fd_in);

    /* ---------------------------------------------------------
       2) Primer pase del padre: convertir letras a mayúsculas
          Y dejar huecos para los dígitos (crecer buffer)
       --------------------------------------------------------- */

    /* Estimación máxima: cada dígito puede generar hasta 9 '*',
       por seguridad multiplicamos por 10 */
    size_t max_out = size_in * 10;
    char *temp_buf = malloc(max_out);
    if (!temp_buf) { perror("malloc"); return 1; }

    size_t pos = 0;

    for (size_t i = 0; i < size_in; i++) {

        if (isalpha(input_buf[i])) {
            temp_buf[pos++] = toupper(input_buf[i]);
        }
        else if (isdigit(input_buf[i])) {
            /* Padre deja 10 espacios en blanco para que el hijo los llene */
            for (int k = 0; k < 10; k++)
                temp_buf[pos++] = '_';   // hueco para hijo
        }
        else {
            temp_buf[pos++] = input_buf[i];
        }
    }

    free(input_buf);

    /* Nuevo tamaño provisional */
    size_t size_mid = pos;

    /* ---------------------------------------------------------
       3) Crear archivo de salida con tamaño size_mid
       --------------------------------------------------------- */
    int fd_out = open(outfile, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd_out < 0) { perror("open salida"); return 1; }

    if (ftruncate(fd_out, size_mid) < 0) {
        perror("ftruncate");
        return 1;
    }

    /* ---------------------------------------------------------
       4) Mapear archivo salida (MAP_SHARED)
       --------------------------------------------------------- */
    char *map = mmap(NULL, size_mid, PROT_READ | PROT_WRITE, MAP_SHARED, fd_out, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    close(fd_out);

    /* Copiar la fase del padre al mapeo */
    memcpy(map, temp_buf, size_mid);

    /* ---------------------------------------------------------
       5) Crear zona de sincronización (MAP_SHARED | MAP_ANONYMOUS)
       --------------------------------------------------------- */
    int *sync = mmap(NULL, sizeof(int)*2, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sync[0] = 0;  // mitad 1 lista
    sync[1] = 0;  // mitad 2 lista

    /* ---------------------------------------------------------
       6) fork()
       --------------------------------------------------------- */
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    /* =========================================================
       ===============   PROCESO HIJO   ========================
       ========================================================= */
    if (pid == 0) {

        /* Espera la señal del padre para la mitad 1 */
        while (sync[0] != SYNC_READY) usleep(1000);

        /* Hijo procesa números → '*' */
        size_t half = size_mid / 2;

        for (size_t i = 0; i < half; i++) {
            if (map[i] == '_') {
                /* reemplazar huecos con '*' */
                for (int k = 0; k < 10; k++)
                    map[i + k] = '*';
            }
        }

        sync[0] = SYNC_DONE;

        /* Espera segunda mitad */
        while (sync[1] != SYNC_READY) usleep(1000);

        for (size_t i = half; i < size_mid; i++) {
            if (map[i] == '_') {
                for (int k = 0; k < 10; k++)
                    map[i + k] = '*';
            }
        }

        sync[1] = SYNC_DONE;

        munmap(map, size_mid);
        munmap(sync, sizeof(int)*2);
        exit(0);
    }

    /* =========================================================
       ===============   PROCESO PADRE   ========================
       ========================================================= */

    /* Padre “avisa” al hijo que puede procesar la primera mitad */
    sync[0] = SYNC_READY;

    /* Espera el hijo termine mitad 1 */
    while (sync[0] != SYNC_DONE) usleep(1000);

    /* Avanza segunda mitad */
    sync[1] = SYNC_READY;

    /* Espera el hijo */
    while (sync[1] != SYNC_DONE) usleep(1000);

    /* ---------------------------------------------------------
       7) Contar asteriscos y añadir línea final al archivo
       --------------------------------------------------------- */

    size_t count_ast = 0;

    for (size_t i = 0; i < size_mid; i++)
        if (map[i] == '*')
            count_ast++;

    char final_line[64];
    int len = snprintf(final_line, sizeof(final_line),
                       "\nTotal asteriscos: %ld\n", count_ast);

    /* Redimensionar archivo para añadir la línea final */
    size_t final_size = size_mid + len;

    int fd2 = open(outfile, O_RDWR);
    ftruncate(fd2, final_size);
    close(fd2);

    /* Remapear con nuevo tamaño */
    map = mremap(map, size_mid, final_size, MREMAP_MAYMOVE);
    memcpy(map + size_mid, final_line, len);

    /* --------------------------------------------------------- */
    munmap(map, final_size);
    munmap(sync, sizeof(int)*2);
    wait(NULL);

    printf("Procesamiento completado.\n");
    return 0;
}
