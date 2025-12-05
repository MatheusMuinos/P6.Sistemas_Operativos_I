#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>     // close, usleep, fork
#include <sys/wait.h>   // wait

#define SYNC_READY  1
#define SYNC_DONE   2

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo_entrada> <archivo_salida>\n", argv[0]);
        return 1;
    }

    char *infile  = argv[1];
    char *outfile = argv[2];

    /* 1) Nombres de archivo diferentes */
    if (strcmp(infile, outfile) == 0) {
        fprintf(stderr, "El archivo de salida debe ser diferente al de entrada.\n");
        return 1;
    }

    /* ---------------------------------------------------------
       2) Abrir archivo de entrada y obtener tamaño
       --------------------------------------------------------- */
    int fd_in = open(infile, O_RDONLY);
    if (fd_in < 0) {
        perror("open entrada");
        return 1;
    }

    struct stat st_in;
    if (fstat(fd_in, &st_in) < 0) {
        perror("fstat entrada");
        close(fd_in);
        return 1;
    }

    size_t size_in = (size_t)st_in.st_size;

    if (size_in == 0) {
        /* Arquivo vazio: só cria saída com a linha final 0 */
        int fd_out_empty = open(outfile, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd_out_empty < 0) {
            perror("open salida");
            close(fd_in);
            return 1;
        }

        char final_line[64];
        int len = snprintf(final_line, sizeof(final_line),
                           "Total asteriscos: 0\n");
        if (len < 0) {
            fprintf(stderr, "Error en snprintf\n");
            close(fd_in);
            close(fd_out_empty);
            return 1;
        }

        size_t line_len = (size_t)len;

        if (ftruncate(fd_out_empty, (off_t)line_len) < 0) {
            perror("ftruncate salida");
            close(fd_in);
            close(fd_out_empty);
            return 1;
        }

        char *map_out0 = mmap(NULL, line_len,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED,
                              fd_out_empty, 0);
        if (map_out0 == MAP_FAILED) {
            perror("mmap salida");
            close(fd_in);
            close(fd_out_empty);
            return 1;
        }

        memcpy(map_out0, final_line, line_len);

        msync(map_out0, line_len, MS_SYNC);
        munmap(map_out0, line_len);
        close(fd_out_empty);
        close(fd_in);
        return 0;
    }

    /* ---------------------------------------------------------
       3) Mapear archivo de entrada (solo lectura)
       --------------------------------------------------------- */
    char *map_in = mmap(NULL, size_in,
                        PROT_READ,
                        MAP_PRIVATE,
                        fd_in, 0);
    if (map_in == MAP_FAILED) {
        perror("mmap entrada");
        close(fd_in);
        return 1;
    }

    close(fd_in);

    /* ---------------------------------------------------------
       4) Primer pase: calcular tamaño intermedio (con huecos)
          letras -> 1 byte
          digitos '0'..'9' -> d huecos '_'
          otros -> 1 byte
       --------------------------------------------------------- */
    size_t size_mid = 0;

    for (size_t i = 0; i < size_in; i++) {
        unsigned char c = (unsigned char)map_in[i];
        if (isdigit(c)) {
            int d = c - '0';
            size_mid += (size_t)d;  // d huecos
        } else {
            size_mid += 1;
        }
    }

    /* Buffer temporal con el contenido transformado (letras mayúsculas,
       dígitos convertidos a '_' para que el hijo los cambie por '*') */
    char *temp_buf = malloc(size_mid);
    if (!temp_buf) {
        perror("malloc temp_buf");
        munmap(map_in, size_in);
        return 1;
    }

    /* ---------------------------------------------------------
       5) Segundo pase: rellenar temp_buf
       --------------------------------------------------------- */
    size_t pos = 0;
    for (size_t i = 0; i < size_in; i++) {
        unsigned char c = (unsigned char)map_in[i];

        if (isalpha(c)) {
            temp_buf[pos++] = (char)toupper(c);
        } else if (isdigit(c)) {
            int d = c - '0';
            for (int k = 0; k < d; k++) {
                temp_buf[pos++] = '_';   // huecos para el hijo
            }
        } else {
            temp_buf[pos++] = (char)c;
        }
    }

    if (pos != size_mid) {
        fprintf(stderr, "Error interno: tamaño intermedio inconsistente.\n");
        free(temp_buf);
        munmap(map_in, size_in);
        return 1;
    }

    munmap(map_in, size_in);

    /* ---------------------------------------------------------
       6) Crear archivo de salida con tamaño size_mid
       --------------------------------------------------------- */
    int fd_out = open(outfile, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd_out < 0) {
        perror("open salida");
        free(temp_buf);
        return 1;
    }

    if (ftruncate(fd_out, (off_t)size_mid) < 0) {
        perror("ftruncate salida");
        free(temp_buf);
        close(fd_out);
        return 1;
    }

    /* ---------------------------------------------------------
       7) Mapear archivo de salida y copiar temp_buf usando memcpy
       --------------------------------------------------------- */
    char *map = mmap(NULL, size_mid,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     fd_out, 0);
    if (map == MAP_FAILED) {
        perror("mmap salida");
        free(temp_buf);
        close(fd_out);
        return 1;
    }

    close(fd_out);

    memcpy(map, temp_buf, size_mid);
    free(temp_buf);

    /* ---------------------------------------------------------
       8) Zona de sincronización compartida (MAP_SHARED | MAP_ANONYMOUS)
       --------------------------------------------------------- */
    int *sync = mmap(NULL, sizeof(int) * 2,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS,
                     -1, 0);
    if (sync == MAP_FAILED) {
        perror("mmap sync");
        munmap(map, size_mid);
        return 1;
    }
    sync[0] = 0;
    sync[1] = 0;

    /* ---------------------------------------------------------
       9) fork() para padre/hijo
       --------------------------------------------------------- */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        munmap(map, size_mid);
        munmap(sync, sizeof(int) * 2);
        return 1;
    }

    /* =========================================================
       ===============   PROCESO HIJO   ========================
       ========================================================= */
    if (pid == 0) {

        /* Espera señal del padre para procesar la primera mitad */
        while (sync[0] != SYNC_READY) {
            usleep(1000);
        }

        size_t half = size_mid / 2;

        /* Primera mitad: '_' -> '*' */
        for (size_t i = 0; i < half; i++) {
            if (map[i] == '_') {
                map[i] = '*';
            }
        }

        sync[0] = SYNC_DONE;

        /* Espera segunda mitad */
        while (sync[1] != SYNC_READY) {
            usleep(1000);
        }

        for (size_t i = half; i < size_mid; i++) {
            if (map[i] == '_') {
                map[i] = '*';
            }
        }

        sync[1] = SYNC_DONE;

        msync(map, size_mid, MS_SYNC);
        munmap(map, size_mid);
        munmap(sync, sizeof(int) * 2);
        exit(0);
    }

    /* =========================================================
       ===============   PROCESO PADRE   =======================
       ========================================================= */

    /* Padre indica al hijo que puede procesar la primera mitad */
    sync[0] = SYNC_READY;

    /* Espera a que el hijo termine la primera mitad */
    while (sync[0] != SYNC_DONE) {
        usleep(1000);
    }

    /* Señal para la segunda mitad */
    sync[1] = SYNC_READY;

    /* Espera a que el hijo termine la segunda mitad */
    while (sync[1] != SYNC_DONE) {
        usleep(1000);
    }

    /* Esperar a que el hijo realmente termine */
    wait(NULL);

    /* ---------------------------------------------------------
       10) Contar asteriscos en la proyección
       --------------------------------------------------------- */
    size_t count_ast = 0;
    for (size_t i = 0; i < size_mid; i++) {
        if (map[i] == '*') {
            count_ast++;
        }
    }

    /* ---------------------------------------------------------
       11) Construir la línea final y ampliar el archivo
       --------------------------------------------------------- */
    char final_line[64];
    int len = snprintf(final_line, sizeof(final_line),
                       "Total asteriscos: %zu\n", count_ast);
    if (len < 0) {
        fprintf(stderr, "Error en snprintf\n");
        munmap(map, size_mid);
        munmap(sync, sizeof(int) * 2);
        return 1;
    }
    size_t line_len = (size_t)len;

    size_t final_size = size_mid + line_len;

    /* Sincronizar y desmapear antes de cambiar el tamaño */
    msync(map, size_mid, MS_SYNC);
    munmap(map, size_mid);

    /* Ampliar fichero de salida e instalar NUEVA proyección */
    int fd2 = open(outfile, O_RDWR);
    if (fd2 < 0) {
        perror("open salida para extension");
        munmap(sync, sizeof(int) * 2);
        return 1;
    }

    if (ftruncate(fd2, (off_t)final_size) < 0) {
        perror("ftruncate salida final");
        close(fd2);
        munmap(sync, sizeof(int) * 2);
        return 1;
    }

    char *map2 = mmap(NULL, final_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd2, 0);
    if (map2 == MAP_FAILED) {
        perror("mmap salida final");
        close(fd2);
        munmap(sync, sizeof(int) * 2);
        return 1;
    }

    close(fd2);

    /* Los primeros size_mid bytes ya contienen el texto procesado.
       Añadimos la línea final al final del archivo. */
    memcpy(map2 + size_mid, final_line, line_len);

    msync(map2, final_size, MS_SYNC);
    munmap(map2, final_size);
    munmap(sync, sizeof(int) * 2);

    printf("Procesamiento completado. Archivo de salida: %s\n", outfile);
    return 0;
}
