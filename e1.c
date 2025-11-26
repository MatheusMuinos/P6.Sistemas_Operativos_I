#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h> // para O_RDONLY
#include <sys/mman.h> // para mmap

int main(int argc, char *argv[]){

    if(argc < 2){
        fprintf(stderr, "Formato invalido. Uso: %s <Nombre_Archivo>\n", argv[0]);
        return 1;
    }
    
    int archivo = open(argv[1], O_RDONLY);
    if (archivo == -1) {
        perror("open");
        return 1;
    }

    // Buffer para fstat
    struct stat st;
    if (fstat(archivo, &st) == -1) {
        perror("fstat");
        close(archivo);
        return 1;
    }
    printf("Tamaño del archivo: %ld bytes\n", st.st_size);
    
    char c;
    for (int i = 0; i < st.st_size; i++){ 
        if (read(archivo, &c, 1) != 1) { //leemos 1 byte del archivo y lo guardamos en c
            perror("read"); // error
            close(archivo);
            return 1;
        }
        putchar(c); // es una función de la biblioteca estándar de C que imprime un solo carácter en la salida estándar
    }

    char *map = mmap(
        NULL,           // Dirección elegida por el kernel
        st.st_size,     // Tamaño a mapear
        PROT_READ,      // Permisos del mapping: Solo lectura
        MAP_PRIVATE,    // Tipo de mapeo: Area de memoria privada
        archivo,        // Descriptor de archivo
        0               // Offset dentro del archivo
    );

    //Comprobar errores
    if (map == MAP_FAILED) {
        perror("mmap");
        close(archivo);
        return 1;
    }

    // Cerrar el archivo después del mmap
    close(archivo);

    // Imprimir la proyección carácter a carácter
    for (size_t i = 0; i < st.st_size; i++) {
        putchar(map[i]);
    }

    //Desmapeo/Desproyectar la memoria
    munmap(map, st.st_size);
    return 0;
}