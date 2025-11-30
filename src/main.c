#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "loading.h"

int main(int argc, char *argv[])
{
    // abrindo arquivos
    if (argc != 3)
    {
        printf("Uso: %s <arquivo_entrada> <arquivo_saida>\n", argv[0]);
        return 1;
    };
    FILE *input = fopen(argv[1], "r");
    if (!input)
    {
        perror("Erro ao abrir arquivo de entrada");
        return 1;
    };
    FILE *output = fopen(argv[2], "w");
    if (!output)
    {
        perror("Erro ao abrir arquivo de saida");
        fclose(input);
        return 1;
    };

    char token[32];
    const uint32_t offset = 0x80000000;
    size_t i = 0;
    // declarando registradores
    uint32_t x[32] = {0};
    const char *x_label[32] = {"zero", "ra", "sp", "gp",
                               "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2",
                               "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
                               "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
    uint32_t pc = offset;
    uint8_t *mem = (uint8_t *)malloc(32 * 1024); // 32 KiB de memória
    if (!mem)
    {
        perror("Erro ao alocar memória");
        fclose(input);
        fclose(output);
        return 1;
    }

    // carregando programa
    loading_memory(input, mem);

    // fechando arquivos
    fclose(input);
    fclose(output);
    return 0;
}