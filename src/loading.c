#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "loading.h"

void loading_memory(FILE *filename, uint8_t *mem)
{
    char token[32];
    uint8_t i = 0;
    while (fscanf(filename, "%31s", token) == 1)
    {
        // Se começa com '@', é um endereço -> apenas ignore
        if (token[0] == '@')
        {
            continue;
        }

        unsigned int valor;
        if (sscanf(token, "%x", &valor) == 1)
        {
            if (i < 32 * 1024)
            {
                mem[i++] = (uint8_t)valor;
            }
        }
    }
}