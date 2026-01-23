#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>     // close, usleep, fork
#include <sys/wait.h>   // wait

int main (int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso correcto: %s <archivo_entrada> <archivo_salida>\n", argv[0]);
        return 1;
    }

    char *entradafile = argv[1];
    char *salidafile = argv[2];

    // comprueba los nombres de archivo diferentes
    if (strcmp(entradafile, salidafile) == 0) {
        fprintf(stderr, "El archivo de salida debe ser diferente al de entrada.\n");
        return 1;
    }

    // abrir entrada archivo y obtener tamaño
    int descriptor_entrada = open(entradafile, O_RDONLY);
    if (descriptor_entrada < 0) {
        perror("open entrada");
        return 1;
    }

    struct stat stat_entrada;
    if (fstat(descriptor_entrada, &stat_entrada) < 0) {
        perror("fstat entrada");
        close(descriptor_entrada);
        return 1;
    }

    size_t tamaño_entrada = (size_t) stat_entrada.st_size;

    // mapear archivo de entrada (solo lectura)
    char *map_entrada = mmap(NULL, tamaño_entrada, PROT_READ, MAP_PRIVATE, descriptor_entrada, 0);
    if (map_entrada == MAP_FAILED) {
        perror("mmap entrada");
        close(descriptor_entrada);
        return 1;
    }
    close(descriptor_entrada);

    // calcular tamaño intermedio
    size_t tamaño_intermedio = 0;
    for (size_t i = 0; i < tamaño_entrada; i++) {
        unsigned char caracter_atual = (unsigned char) map_entrada[i];
        if (isdigit(caracter_atual)) { // caracter atual es un número
            int digitos = caracter_atual - '0';
            tamaño_intermedio += (size_t)digitos;
        } else {
            tamaño_intermedio += 1;
        }
    }

    // buffer temporal compartido (no es el archivo)
    char *buffer_compartido = mmap(NULL, tamaño_intermedio, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);   
    if (buffer_compartido == MAP_FAILED) {
        perror("mmap buffer_compartido");
        munmap(map_entrada, tamaño_entrada);
        return 1;
    }

    // crear archivo de salida con tamaño tamaño_intermedio
    int descriptor_salida = open(salidafile, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (descriptor_salida < 0) {
        perror("open salida");
        munmap(buffer_compartido, tamaño_intermedio);
        munmap(map_entrada, tamaño_entrada);
        return 1;
    }

    // espacio extra al final para variable de sincronización (int)
    size_t tamaño_total_map = tamaño_intermedio + sizeof(int);

    // Asignar tamaño al archivo físico de salida
    if (ftruncate(descriptor_salida, tamaño_total_map) == -1) {
        perror("ftruncate salida");
        munmap(buffer_compartido, tamaño_intermedio);
        munmap(map_entrada, tamaño_entrada);
        close(descriptor_salida);
        return 1;
    }

    // Proyectar archivo de salida en memoria (COMPARTIDO para lectura/escritura)
    char *map_salida = mmap(NULL, tamaño_total_map, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_salida, 0);
    if (map_salida == MAP_FAILED) {
        perror("mmap salida");
        munmap(buffer_compartido, tamaño_intermedio);
      munmap(map_entrada, tamaño_entrada);
        close(descriptor_salida);
        return 1;
    }
    close(descriptor_salida); // Ya no necesitamos el descriptor

    // Puntero a la bandera de sincronización (situada al final del archivo temporalmente)
    volatile int *sync_flag = (int *)(map_salida + tamaño_intermedio);
    *sync_flag = 0; // 0: Inicio, 1: Mitad lista, 2: Todo listo

    // punto medio del archivo de ENTRADA
    size_t mitad_entrada = tamaño_entrada / 2;

    // Crear proceso hijo
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        munmap(buffer_compartido, tamaño_intermedio);
        munmap(map_entrada, tamaño_entrada);
        munmap(map_salida, tamaño_total_map);
        return 1;
    } else if (pid == 0) {
        // --- PROCESO HIJO (Maneja los NÚMEROS -> ASTERISCOS) ---

        // Esperar a que el padre procese la primera mitad
        while (*sync_flag < 1) usleep(1000);

        size_t pos_salida = 0;

        // Recorremos todo el archivo, pero solo actuamos si encontramos números
        for (size_t i = 0; i < tamaño_entrada; i++) {

            // Pausa de sincronización al llegar a la segunda mitad
            if (i == mitad_entrada)
                while (*sync_flag < 2) usleep(1000); // Esperar a que padre termine todo

            unsigned char c = (unsigned char) map_entrada[i];

            if (isdigit(c)) {
                int num_asteriscos = c - '0';
                // Escribimos '*' repetidamente en el buffer temporal
                memset(buffer_compartido + pos_salida, '*', num_asteriscos);
                pos_salida += num_asteriscos;
            }
            else if (isalpha(c)) {
                // El padre ya escribió (o escribirá), solo avanzamos
                pos_salida += 1;
            }
            else {
                pos_salida += 1;
            }
        }

        // hijo termina; limpia sus mappings
        munmap(map_entrada, tamaño_entrada);
        munmap(buffer_compartido, tamaño_intermedio);
        munmap(map_salida, tamaño_total_map);
        exit(0);
    } else {
        // --- PROCESO PADRE (Maneja LETRAS -> MAYÚSCULAS) ---

        size_t pos_salida = 0;

        for (size_t i = 0; i < tamaño_entrada; i++) {

            // Si llegamos a la mitad, avisamos al hijo
            if (i == mitad_entrada) {
                *sync_flag = 1; // primera mitad lista
                msync(map_salida, tamaño_total_map, MS_SYNC);
            }

            unsigned char c = (unsigned char) map_entrada[i];

            if (isalpha(c)) {
                buffer_compartido[pos_salida] = toupper(c);
                pos_salida += 1;
            }
            else if (isdigit(c)) {
                int num_asteriscos = c - '0';
                pos_salida += num_asteriscos; // sólo reservamos hueco
            }
            else {
                buffer_compartido[pos_salida] = (char) c;
                pos_salida += 1;
            }
        }

        // Fin del procesamiento del padre
        *sync_flag = 2; // todo listo
        msync(map_salida, tamaño_total_map, MS_SYNC);

        // Esperar a que el hijo termine
        wait(NULL);

        // 4. FASE FINAL: Recuento de asteriscos en buffer_compartido
        int total_asteriscos = 0;
        for (size_t k = 0; k < tamaño_intermedio; k++) {
            if (buffer_compartido[k] == '*') {
                total_asteriscos++;
            }
        }

        // Preparamos el mensaje final
        char buffer_contador_asteriscos[64];
        int tam_contador = sprintf(buffer_contador_asteriscos,
                                   "\nTotal asteriscos: %d\n",
                                   total_asteriscos);

        size_t tamaño_final = tamaño_intermedio + (size_t)tam_contador;

        // Desmapeamos mapa de entrada y salida "antiguo"
        munmap(map_entrada, tamaño_entrada);
        munmap(map_salida, tamaño_total_map);

        // Redimensionar archivo de salida al tamaño final
        descriptor_salida = open(salidafile, O_RDWR);
        if (descriptor_salida < 0) {
            perror("open salida final");
            munmap(buffer_compartido, tamaño_intermedio);
            return 1;
        }
        if (ftruncate(descriptor_salida, tamaño_final) == -1) {
            perror("ftruncate salida final");
            munmap(buffer_compartido, tamaño_intermedio);
            close(descriptor_salida);
            return 1;
        }

        // Nueva proyección del archivo final
        map_salida = mmap(NULL, tamaño_final,
                          PROT_WRITE | PROT_READ,
                          MAP_SHARED, descriptor_salida, 0);
        if (map_salida == MAP_FAILED) {
            perror("mmap salida final");
            munmap(buffer_compartido, tamaño_intermedio);
            close(descriptor_salida);
            return 1;
        }

        // Copiamos el contenido del buffer temporal al archivo
        memcpy(map_salida, buffer_compartido, tamaño_intermedio);
        // Copiamos el footer al final
        memcpy(map_salida + tamaño_intermedio,
               buffer_contador_asteriscos,
               (size_t)tam_contador);

        msync(map_salida, tamaño_final, MS_SYNC);
        munmap(map_salida, tamaño_final);
        munmap(buffer_compartido, tamaño_intermedio);
        close(descriptor_salida);

        printf("Proceso completado. Archivo generado: %s\n", salidafile);
    }

    return 0;
}
