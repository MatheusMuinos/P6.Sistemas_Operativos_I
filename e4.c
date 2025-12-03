#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>      // open
#include <sys/mman.h>   // mmap
#include <unistd.h>     // close, getpid

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Formato invalido. Uso: %s <Nombre_Archivo>\n", argv[0]);
        return 1;
    }

    // Mostramos el PID para poder ver /proc/PID/maps y usar pmap
    printf("PID del proceso: %d\n", getpid());
    printf("Archivo a mapear: %s\n", argv[1]);

    printf("\n>>> ANTES del mmap\n");
    printf("Abra otra terminal y ejecute:\n");
    printf("    pmap -x %d\n", getpid());
    printf("Despues vuelva aqui y presione ENTER para seguir...\n");
    getchar();

    // Abrimos el archivo con lectura y ESCRITURA
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

    // Proyectamos el archivo en memoria
    char *map = mmap(
        NULL,                   // dirección elegida por el kernel
        st.st_size,             // tamaño a mapear
        PROT_READ | PROT_WRITE, // permisos
        MAP_SHARED,             // zona compartida (cambios -> archivo)
        archivo,                // descriptor de archivo
        0                       // offset dentro del archivo
    );

    if (map == MAP_FAILED) {
        perror("mmap");
        close(archivo);
        return 1;
    }

    // Ya no necesitamos el descriptor abierto para trabajar con el mapping
    close(archivo);

    printf("\n>>> DESPUÉS del mmap\n");
    printf("En la otra terminal, ejecute de nuevo:\n");
    printf("    pmap -x %d\n", getpid());
    printf("Fijese en la nueva linea con la ruta del archivo.\n");
    printf("Cuando termine de mirar, pulse ENTER aqui...\n");
    getchar();

    // Imprimimos el contenido original del archivo desde la proyección
    printf("\nContenido original (desde la proyeccion):\n");
    for (size_t i = 0; i < (size_t)st.st_size; i++) {
        putchar(map[i]);
    }
    putchar('\n');

    // Modificamos un carácter de la proyección
    printf("\nModificando el primer caracter del archivo en la proyeccion...\n");
    char original = map[0];
    map[0] = (original == 'X') ? 'Y' : 'X';  // alterna entre 'X' y 'Y' para verlo fácil

    printf("\n>>> Despues de modificar en memoria, ANTES de msync\n");
    printf("mira el archivo.txt\n");
    printf("El contenido antiguo del archivo sigue alli.\n");
    printf("Cuando termine, pulse ENTER aqui...\n");
    getchar();

    printf("\nContenido original antes del msync cambiar en la proyeccion:\n");
    for (size_t i = 0; i < (size_t)st.st_size; i++) {
        putchar(map[i]);
    }
    putchar('\n');

    // Forzamos a que los cambios se sincronicen con el archivo
    printf("\nLlamando a msync para sincronizar la proyeccion con el archivo...\n\n");
    if (msync(map, st.st_size, MS_SYNC) == -1) {
        perror("msync");
        munmap(map, st.st_size);
        return 1;
    }

    printf("\n>>> Despues de msync, ANTES de munmap\n");
    printf("mira el archivo.txt\n");
    printf("Ahora deberia ver el primer caracter YA CAMBIADO.\n");
    printf("Cuando termine, pulse ENTER aqui...\n");
    getchar();

    // Mostramos el contenido modificado desde la proyección
    printf("\nContenido despues de la modificacion en memoria:\n");
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
