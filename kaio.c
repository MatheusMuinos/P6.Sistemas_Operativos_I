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

    char *entradafile  = argv[1];
    char *salidafile = argv[2];

    // comprueba los nombres de archivo diferentes
    if (strcmp (entradafile, salidafile) == 0) {
        fprintf (stderr, "El archivo de salida debe ser diferente al de entrada.\n");
        return 1;
    }

    // abrir entrada archivo y obtener tamaño
    int descriptor_entrada = open (entradafile, O_RDONLY);
    if (descriptor_entrada < 0) {
        perror ("open entrada");
        return 1;
    }

    struct stat stat_entrada;
    if (fstat (descriptor_entrada, &stat_entrada) < 0) {
        perror ("fstat entrada");
        close (descriptor_entrada);
        return 1;
    }

    size_t tamaño_entrada = (size_t) stat_entrada.st_size;

    // mapear archivo de entrada (solo lectura)
    char *map_entrada = mmap (NULL, tamaño_entrada, PROT_READ, MAP_PRIVATE, descriptor_entrada, 0);
    close (descriptor_entrada);

    // calcular tamaño intermedio
    size_t tamaño_intermedio = 0;
    for (size_t i = 0; i < tamaño_entrada; i++) {
        unsigned char caracter_atual = (unsigned char) map_entrada[i];
        if (isdigit(caracter_atual)) { // para si el isdigit es verdadero, o sea, el caracter atual es un número
            int digitos = caracter_atual - '0';
            tamaño_intermedio += (size_t)digitos;
        } else {
            tamaño_intermedio += 1;
        }
    }

    // crear archivo de salida con tamaño tamaño_intermedio que viene del archivo de entrada
    int descriptor_salida = open (salidafile, O_RDWR | O_CREAT | O_TRUNC, 0666); // haz el truncate para que no tenga nada
    if (descriptor_salida < 0) {
        perror("open salida");
        return 1;
    }

    // Necesitamos espacio extra al final para una variable de sincronización (entero)
    // que permita al hijo saber cuándo el padre va por la mitad.
    size_t tamaño_total_map = tamaño_intermedio + sizeof(int);

    // Asignar tamaño al archivo físico de salida
    if (ftruncate(descriptor_salida, tamaño_total_map) == -1){ // ftruncate: Cambia el tamaño de un archivo asociado a un descriptor de archivo
        perror("ftruncate salida"); // error
        return 1;
    }

    // Proyectar archivo de salida en memoria (COMPARTIDO para lectura/escritura)
    // Usamos MAP_SHARED para que los cambios del padre se vean en el hijo y viceversa.
    char *map_salida = mmap(NULL, tamaño_total_map, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_salida, 0);
    if (map_salida == MAP_FAILED) {
        perror("mmap salida");
        close(descriptor_salida);
        return 1;
    }
    close(descriptor_salida); // Ya no necesitamos el descriptor

    // Puntero a la bandera de sincronización (situada al final del archivo temporalmente)
    volatile int *sync_flag = (int *)(map_salida + tamaño_intermedio);
    *sync_flag = 0; // 0: Inicio, 1: Mitad lista, 2: Todo listo

    // Definimos el punto medio del archivo de ENTRADA para la sincronización tanto en el padre como en el hijo
    size_t mitad_entrada = tamaño_entrada / 2;

    // Crear proceso hijo
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    } else if (pid == 0){
        // --- PROCESO HIJO (Maneja los NÚMEROS -> ASTERISCOS) ---

        // Esperar a que el padre procese la primera mitad
        // "Chequeando los valores de la zona de memoria compartida" 
        while (*sync_flag < 1) usleep(1000);

        size_t pos_salida = 0;

        // Recorremos todo el archivo, pero solo actuamos si encontramos números
        for (size_t i = 0; i < tamaño_entrada; i++) {
            
            // Pausa de sincronización al llegar a la segunda mitad
            if (i == mitad_entrada) while (*sync_flag < 2) usleep(1000); // Esperar a que padre termine todo

            unsigned char c = (unsigned char) map_entrada[i];

            if (isdigit(c)) {
                // Es un número: El hijo escribe los asteriscos
                int num_asteriscos = c - '0';
                // Escribimos '*' repetidamente
                // Usamos memset para simular la construcción del string temporal en esa zona
                memset(map_salida + pos_salida, '*', num_asteriscos);
                pos_salida += num_asteriscos;
            } 
            else if (isalpha(c)) {
                // Es letra: El padre ya escribió (o escribirá), solo avanzamos el índice
                pos_salida += 1;
            } 
            else {
                // Otros caracteres: Avanzamos
                pos_salida += 1;
            }
        }
        exit(0); // El hijo termina aquí
    } else {
        // --- PROCESO PADRE (Maneja LETRAS -> MAYÚSCULAS) ---
        
        size_t pos_salida = 0;

        for (size_t i = 0; i < tamaño_entrada; i++) {
            
            // Si llegamos a la mitad, avisamos al hijo
            if (i == mitad_entrada) {
                *sync_flag = 1; // Señal de primera mitad lista
                msync(map_salida, tamaño_total_map, MS_SYNC); // Asegurar escritura
            }

            unsigned char c = (unsigned char) map_entrada[i];

            if (isalpha(c)) {
                // Es letra: El padre la pasa a mayúscula
                map_salida[pos_salida] = toupper(c);
                pos_salida += 1;
            } 
            else if (isdigit(c)) {
                // Es número: El padre deja el hueco 
                int num_asteriscos = c - '0';
                pos_salida += num_asteriscos; // Saltamos los índices para reservar espacio
            } 
            else {
                // Otros caracteres: Copiamos tal cual
                map_salida[pos_salida] = (char) c;
                pos_salida += 1;
            }
        }

        // Fin del procesamiento del padre
        *sync_flag = 2; // Señal de todo listo
        msync(map_salida, tamaño_total_map, MS_SYNC);

        // Esperar a que el hijo termine de rellenar sus asteriscos
        wait(NULL);

        // 4. FASE FINAL: Recuento y escritura del numero de asteriscos
        
        int total_asteriscos = 0;
        for (size_t k = 0; k < tamaño_intermedio; k++) {
            if (map_salida[k] == '*') {
                total_asteriscos++;
            }
        }

        // Preparamos el mensaje final
        char buffer_contador_asteriscos[64];
        int tam_contador = sprintf(buffer_contador_asteriscos, "\nTotal asteriscos: %d\n", total_asteriscos);

        // Quitamos la variable de sync (int) y añadimos el tamaño del footer
        // El nuevo tamaño real del archivo debe ser: contenido_texto + footer
        size_t tamaño_final = tamaño_intermedio + tam_contador;

        // Desmapeamos para redimensionar limpiamente
        munmap(map_entrada, tamaño_entrada);
        munmap(map_salida, tamaño_total_map);

        // Truncamos al tamaño final exacto (borra el int de sync y hace sitio para el texto) 
        descriptor_salida = open(salidafile, O_RDWR);
        ftruncate(descriptor_salida, tamaño_final);

        // Volvemos a mapear o usamos write para el final (write es más simple para append final)
        // Pero siguiendo la práctica de usar proyecciones:
        map_salida = mmap(NULL, tamaño_final, PROT_WRITE, MAP_SHARED, descriptor_salida, 0);
        if (map_salida != MAP_FAILED) {
            // Copiamos el footer al final del contenido original
            memcpy(map_salida + tamaño_intermedio, buffer_contador_asteriscos, tam_contador);
            munmap(map_salida, tamaño_final);
        }
        
        close(descriptor_salida);
        
        printf("Proceso completado. Archivo generado: %s\n", salidafile);
    }
    return 0;
}