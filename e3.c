#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>      // open
#include <sys/mman.h>   // mmap
#include <unistd.h>     // read, close, getpid

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Formato invalido. Uso: %s <Nombre_Archivo>\n", argv[0]);
        return 1;
    }

    // Mostramos o PID para poder ver /proc/PID/maps
    printf("PID del proceso: %d\n", getpid());
    printf("Archivo a mapear: %s\n", argv[1]);

    printf("\n>>> ANTES del mmap\n");
    printf("Abra otra terminal e rode: pmap -x %d\n", getpid());
    printf("Después vuelva aquí y presione ENTER para seguir\n");
    getchar();

    // Abrimos el archivo con lectura y ESCRITA
    int archivo = open(argv[1], O_RDWR);
    if (archivo == -1) {
        perror("open");
        return 1;
    }

    // Obtenemos el tamaño del archivo con fstat
    struct stat st;
    if (fstat(archivo, &st) == -1) {
        perror("fstat");
        close(archivo);
        return 1;
    }

    if (st.st_size == 0) {
        fprintf(stderr, "El archivo está vacío, no se puede mapear.\n");
        close(archivo);
        return 1;
    }

    printf("Tamaño del archivo: %ld bytes\n", st.st_size);

    // Proyectamos el archivo en memoria
    char *map = mmap(
        NULL,                   // elegida por el kernel
        st.st_size,             // Tamano a mapear
        PROT_READ | PROT_WRITE, // PERMISOS (trocar aqui)
        MAP_SHARED,             // PRIVADO o COMPARTIDO (trocar aqui)
        archivo,                // Descriptor de archivo
        0                       // Offset dentro del archivo
    );

    if (map == MAP_FAILED) {
        perror("mmap");
        close(archivo);
        return 1;
    }

    // Ya no necesitamos el descriptor abierto para trabajar con el mapping
    close(archivo);

    printf("\n>>> DESPUÉS del mmap\n");
    printf("En la otra terminal, mira ahora ejecuta: pmap -x %d\n   ", getpid());
    printf("Fíjate en la nueva línea con la ruta del archivo.\n");
    printf("Cuando termines de mirar, pulsa ENTER aquí...\n");
    getchar();

    // Imprimimos el contenido original del archivo desde la proyección
    printf("\nContenido original:\n");
    for (size_t i = 0; i < (size_t)st.st_size; i++) {
        putchar(map[i]);
    }
    putchar('\n');

    // Modificamos un carácter de la proyección
    printf("\nModificando el primer carácter del archivo en la proyección...\n");
    char original = map[0];
    map[0] = (original == 'X') ? 'Y' : 'X';  // Alterna entre 'X' y 'Y' para verlo fácil

    // Forzamos a que los cambios se sincronicen con el archivo
    if (msync(map, st.st_size, MS_SYNC) == -1) {
        perror("msync");
        munmap(map, st.st_size);
        return 1;
    }

    // Mostramos el contenido modificado desde la proyección
    printf("\nContenido después de la modificación en memoria:\n");
    for (size_t i = 0; i < (size_t)st.st_size; i++) {
        putchar(map[i]);
    }
    putchar('\n');

    // Cerramos (desmapeamos) la proyección
    if (munmap(map, st.st_size) == -1) {
        perror("munmap");
        return 1;
    }

    return 0;
}
