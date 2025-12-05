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

int main (int argc, char *argv[]) {

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

    // crear buffer temporal para el contenido intermedio
    char *buffer_temporal_intermedio = malloc (tamaño_intermedio);

    // llenar buffer temporal intermedio
    size_t posición = 0;
    for (size_t i = 0; i < tamaño_entrada; i++) {
        unsigned char caracter_atual = (unsigned char) map_entrada[i]; //
    
        if (isalpha(caracter_atual)) { // mira si es una letra
            buffer_temporal_intermedio[posición++] = (char) toupper(caracter_atual); // convierte a mayúscula
        }
        else if (isdigit(caracter_atual)) { // mira si es un dígito
            int digitos = caracter_atual - '0'; // convierte el carácter a su valor numérico
            for (int k = 0; k < digitos; k++) {
                buffer_temporal_intermedio[posición++] = '_'; // agrega los huecos
            }
        } else {
            buffer_temporal_intermedio[posición++] = (char) caracter_atual; // copia otros caracteres tal cual
        }
    }

     munmap (map_entrada, tamaño_entrada); // desmapear archivo de entrada

    // crear archivo de salida con tamaño tamaño_intermedio que viene del archivo de entrada
    int descriptor_salida = open (salidafile, O_RDWR | O_CREAT | O_TRUNC, 0666); // haz el truncate para que no tenga nada


}