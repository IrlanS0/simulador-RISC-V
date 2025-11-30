#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// --- Constantes ---
#define MEM_SIZE_BYTES (32 * 1024) // 32 KiB
#define NUM_REGISTERS 32

// --- Estrutura do Simulador ---

/**
 * @brief Mantém todo o estado do simulador em um único lugar.
 */
typedef struct
{
    uint32_t x[NUM_REGISTERS];          // Bancada de registradores
    const char *x_label[NUM_REGISTERS]; // Nomes (ABI) dos registradores
    uint8_t *mem;                       // Ponteiro para a memória (RAM)
    uint32_t pc;                        // Program Counter
    uint32_t offset;                    // Offset da memória (0x80000000)
    uint8_t run;                        // Flag de execução
    FILE *output;                       // Ponteiro para o arquivo de log de saída
} Simulator;

// --- Funções Auxiliares (Helpers) ---

/**
 * @brief Escreve no registrador de destino, ignorando escritas em x0 (zero).
 */
void write_reg(Simulator *sim, uint8_t rd, uint32_t data)
{
    if (rd != 0)
    {
        sim->x[rd] = data;
    }
}

/**
 * @brief Formata e imprime a linha de log no arquivo de saída.
 */
void print_log(Simulator *sim, const char *name, const char *col2, const char *col3)
{
    char col1_addr[20];
    sprintf(col1_addr, "0x%08x:%s", sim->pc, name);
    fprintf(sim->output, "%-18s%-20s%s\n", col1_addr, col2, col3);
}

// --- Funções de Decodificação de Imediatos ---

// Estende o sinal de um imediato de 12 bits (Tipo I)
int32_t get_simm_i(uint16_t imm)
{
    return (imm & 0x800) ? (0xFFFFF000 | imm) : imm;
}

// Estende o sinal de um imediato de 12 bits (Tipo S)
int32_t get_simm_s(uint16_t s_imm)
{
    return (s_imm & 0x800) ? (0xFFFFF000 | s_imm) : s_imm;
}

// Estende o sinal de um imediato de 13 bits (Tipo B)
int32_t get_simm_b(uint16_t b_imm)
{
    return (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : b_imm;
}

// Estende o sinal de um imediato de 21 bits (Tipo J)
int32_t get_simm_j(uint32_t instruction)
{
    const uint32_t imm20 = (instruction & 0x80000000) >> 11;   // imm[20]
    const uint32_t imm10_1 = (instruction & 0x7FE00000) >> 20; // imm[10:1]
    const uint32_t imm11 = (instruction & 0x00100000) >> 9;    // imm[11]
    const uint32_t imm19_12 = (instruction & 0x000FF000);      // imm[19:12]

    const uint32_t imm_j_raw = imm20 | imm19_12 | imm11 | imm10_1;
    return (imm_j_raw & 0x100000) ? (0xFFE00000 | imm_j_raw) : imm_j_raw;
}

// Obtém o valor de um imediato de 20 bits (Tipo U)
int32_t get_imm_u(uint32_t instruction)
{
    return (int32_t)(instruction & 0xFFFFF000);
}

// --- Lógica Principal do Simulador ---

/**
 * @brief Carrega o programa (hex) do arquivo de entrada para a memória do simulador.
 */
void load_memory(Simulator *sim, FILE *input)
{
    char token[32];
    size_t i = 0;
    while (fscanf(input, "%31s", token) == 1)
    {
        if (token[0] == '@')
        {
            continue; // Ignora diretivas de endereço
        }

        unsigned int valor;
        if (sscanf(token, "%x", &valor) == 1)
        {
            if (i < MEM_SIZE_BYTES)
            {
                sim->mem[i++] = (uint8_t)valor;
            }
        }
    }
}

/**
 * @brief Contém o loop principal de fetch-decode-execute.
 */
void simulate(Simulator *sim)
{
    char col2_inst[30]; // Buffers para strings de log
    char col3_details[60];

    sim->run = 1;
    while (sim->run)
    {
        if (sim->pc < sim->offset)
        {
            printf("error: pc out of bounds at pc = 0x%08x\n", sim->pc);
            break;
        }

        // 1. FETCH (Seguro contra alinhamento)
        const uint32_t mem_addr_pc = sim->pc - sim->offset;
        const uint32_t instruction = (uint32_t)sim->mem[mem_addr_pc] |
                                     (uint32_t)(sim->mem[mem_addr_pc + 1] << 8) |
                                     (uint32_t)(sim->mem[mem_addr_pc + 2] << 16) |
                                     (uint32_t)(sim->mem[mem_addr_pc + 3] << 24);

        // 2. DECODE (Campos básicos)
        const uint8_t opcode = instruction & 0x7F;
        const uint8_t funct7 = instruction >> 25;
        const uint8_t rs1 = (instruction >> 15) & 0x1F;
        const uint8_t rs2 = (instruction >> 20) & 0x1F;
        const uint8_t funct3 = (instruction >> 12) & 0x7;
        const uint8_t rd = (instruction >> 7) & 0x1F;

        // Decodifica campos de imediato (pode ser otimizado para só decodificar o tipo certo)
        const uint16_t imm_i = (instruction >> 20) & 0xFFF;
        const uint16_t imm_s = ((funct7 & 0xFE0) << 5) | (rd & 0x1F);
        const uint16_t imm_b = ((funct7 & 0x80) << 5) | ((rd & 0x1E) << 7) | ((funct7 & 0x7E) << 1) | ((rd & 0x1) << 11);
        const uint8_t shamt = (instruction >> 20) & 0x1F; // para shifts (uimm)

        // 3. EXECUTE
        switch (opcode)
        {

        // --- Tipo R ---
        case 0b0110011:
        {
            if (funct7 == 0b0000000)
            { // RV32I
                if (funct3 == 0b000)
                { // add
                    const uint32_t data = sim->x[rs1] + sim->x[rs2];
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "add", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b001)
                {                                                              // sll
                    const uint32_t data = sim->x[rs1] << (sim->x[rs2] & 0x1F); // RV32: só 5 bits importam
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x<<0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "sll", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b010)
                { // slt
                    const uint32_t data = ((int32_t)sim->x[rs1] < (int32_t)sim->x[rs2]) ? 1 : 0;
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "slt", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b011)
                { // sltu
                    const uint32_t data = (sim->x[rs1] < sim->x[rs2]) ? 1 : 0;
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "sltu", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b100)
                { // xor
                    const uint32_t data = sim->x[rs1] ^ sim->x[rs2];
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "xor", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b101)
                { // srl
                    const uint32_t data = sim->x[rs1] >> (sim->x[rs2] & 0x1F);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x>>0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "srl", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b110)
                { // or
                    const uint32_t data = sim->x[rs1] | sim->x[rs2];
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "or", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b111)
                { // and
                    const uint32_t data = sim->x[rs1] & sim->x[rs2];
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "and", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
            }
            else if (funct7 == 0b0100000)
            { // RV32I (sub/sra)
                if (funct3 == 0b000)
                { // sub
                    const uint32_t data = sim->x[rs1] - sim->x[rs2];
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x-0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "sub", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b101)
                { // sra
                    const uint32_t data = (int32_t)sim->x[rs1] >> (sim->x[rs2] & 0x1F);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x>>0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "sra", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
            }
            else if (funct7 == 0b0000001)
            { // RV32M
                if (funct3 == 0b000)
                { // mul
                    const int32_t data_low = (int32_t)((int64_t)(int32_t)sim->x[rs1] * (int64_t)(int32_t)sim->x[rs2]);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data_low);
                    print_log(sim, "mul", col2_inst, col3_details);
                    write_reg(sim, rd, data_low);
                }
                else if (funct3 == 0b001)
                { // mulh
                    const uint32_t data_high = (uint32_t)(((int64_t)(int32_t)sim->x[rs1] * (int64_t)(int32_t)sim->x[rs2]) >> 32);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data_high);
                    print_log(sim, "mulh", col2_inst, col3_details);
                    write_reg(sim, rd, data_high);
                }
                else if (funct3 == 0b010)
                { // mulhsu
                    const uint32_t data_high = (uint32_t)(((int64_t)(int32_t)sim->x[rs1] * (uint64_t)sim->x[rs2]) >> 32);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data_high);
                    print_log(sim, "mulhsu", col2_inst, col3_details);
                    write_reg(sim, rd, data_high);
                }
                else if (funct3 == 0b011)
                { // mulhu
                    const uint32_t data_high = (uint32_t)(((uint64_t)sim->x[rs1] * (uint64_t)sim->x[rs2]) >> 32);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data_high);
                    print_log(sim, "mulhu", col2_inst, col3_details);
                    write_reg(sim, rd, data_high);
                }
                else if (funct3 == 0b100)
                { // div
                    const int32_t dividend = (int32_t)sim->x[rs1];
                    const int32_t divisor = (int32_t)sim->x[rs2];
                    uint32_t data;
                    if (divisor == 0)
                        data = 0xFFFFFFFF; // Divisão por zero
                    else if (dividend == 0x80000000 && divisor == -1)
                        data = 0x80000000; // Overflow
                    else
                        data = (uint32_t)(dividend / divisor);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "div", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b101)
                { // divu
                    const uint32_t divisor = sim->x[rs2];
                    const uint32_t data = (divisor == 0) ? 0xFFFFFFFF : sim->x[rs1] / divisor;
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "divu", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b110)
                { // rem
                    const int32_t dividend = (int32_t)sim->x[rs1];
                    const int32_t divisor = (int32_t)sim->x[rs2];
                    uint32_t data;
                    if (divisor == 0)
                        data = (uint32_t)dividend; // Resto da divisão por zero
                    else if (dividend == 0x80000000 && divisor == -1)
                        data = 0; // Overflow
                    else
                        data = (uint32_t)(dividend % divisor);
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x%%%0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "rem", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct3 == 0b111)
                { // remu
                    const uint32_t divisor = sim->x[rs2];
                    const uint32_t data = (divisor == 0) ? sim->x[rs1] : sim->x[rs1] % divisor;
                    sprintf(col2_inst, "%s,%s,%s", sim->x_label[rd], sim->x_label[rs1], sim->x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x%%%0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], sim->x[rs2], data);
                    print_log(sim, "remu", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
            }
            break;
        }

        // --- Tipo I (Aritmético/Lógico) ---
        case 0b0010011:
        {
            const int32_t simm = get_simm_i(imm_i); // Extensão de sinal feita UMA VEZ

            if (funct3 == 0b000)
            { // addi
                const uint32_t data = sim->x[rs1] + simm;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], simm, data);
                print_log(sim, "addi", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b010)
            { // slti
                const uint32_t data = ((int32_t)sim->x[rs1] < simm) ? 1 : 0;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", sim->x_label[rd], sim->x[rs1], simm, data);
                print_log(sim, "slti", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b011)
            { // sltiu
                const uint32_t data = (sim->x[rs1] < (uint32_t)simm) ? 1 : 0;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", sim->x_label[rd], sim->x[rs1], simm, data);
                print_log(sim, "sltiu", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b100)
            { // xori
                const uint32_t data = sim->x[rs1] ^ simm;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], simm, data);
                print_log(sim, "xori", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b110)
            { // ori
                const uint32_t data = sim->x[rs1] | simm;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], simm, data); // LOG CORRIGIDO
                print_log(sim, "ori", col2_inst, col3_details);                                              // NOME CORRIGIDO
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b111)
            { // andi
                const uint32_t data = sim->x[rs1] & simm;
                sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", sim->x_label[rd], sim->x[rs1], simm, data); // LOG CORRIGIDO
                print_log(sim, "andi", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b001)
            { // slli (RV32I)
                const uint32_t data = sim->x[rs1] << shamt;
                sprintf(col2_inst, "%s,%s,%u", sim->x_label[rd], sim->x_label[rs1], shamt);
                sprintf(col3_details, "%s=0x%08x<<%u=0x%08x", sim->x_label[rd], sim->x[rs1], shamt, data);
                print_log(sim, "slli", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b101)
            { // srli/srai (RV32I)
                if (funct7 == 0b0000000)
                { // srli
                    const uint32_t data = sim->x[rs1] >> shamt;
                    sprintf(col2_inst, "%s,%s,%u", sim->x_label[rd], sim->x_label[rs1], shamt);
                    sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", sim->x_label[rd], sim->x[rs1], shamt, data);
                    print_log(sim, "srli", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
                else if (funct7 == 0b0100000)
                { // srai
                    const uint32_t data = (int32_t)sim->x[rs1] >> shamt;
                    sprintf(col2_inst, "%s,%s,%u", sim->x_label[rd], sim->x_label[rs1], shamt);
                    sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", sim->x_label[rd], sim->x[rs1], shamt, data);
                    print_log(sim, "srai", col2_inst, col3_details);
                    write_reg(sim, rd, data);
                }
            }
            break;
        }

        // --- Tipo I (Loads) ---
        case 0b0000011:
        {
            const int32_t simm = get_simm_i(imm_i);
            const uint32_t address = sim->x[rs1] + simm;
            const uint32_t mem_addr = address - sim->offset;

            if (funct3 == 0b000)
            { // lb
                const int8_t data = (int8_t)sim->mem[mem_addr]; // Seguro
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rd], imm_i, sim->x_label[rs1]);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", sim->x_label[rd], address, data);
                print_log(sim, "lb", col2_inst, col3_details);
                write_reg(sim, rd, (int32_t)data); // Sign-extend
            }
            else if (funct3 == 0b001)
            { // lh (CORRIGIDO)
                const int16_t data = (int16_t)((uint16_t)sim->mem[mem_addr] |
                                               (uint16_t)(sim->mem[mem_addr + 1] << 8));
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rd], imm_i, sim->x_label[rs1]);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", sim->x_label[rd], address, data);
                print_log(sim, "lh", col2_inst, col3_details);
                write_reg(sim, rd, (int32_t)data); // Sign-extend
            }
            else if (funct3 == 0b010)
            { // lw (CORRIGIDO)
                const uint32_t data = (uint32_t)sim->mem[mem_addr] |
                                      (uint32_t)(sim->mem[mem_addr + 1] << 8) |
                                      (uint32_t)(sim->mem[mem_addr + 2] << 16) |
                                      (uint32_t)(sim->mem[mem_addr + 3] << 24);
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rd], imm_i, sim->x_label[rs1]);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", sim->x_label[rd], address, data);
                print_log(sim, "lw", col2_inst, col3_details);
                write_reg(sim, rd, data);
            }
            else if (funct3 == 0b100)
            { // lbu
                const uint8_t data = sim->mem[mem_addr]; // Seguro
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rd], imm_i, sim->x_label[rs1]);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", sim->x_label[rd], address, data);
                print_log(sim, "lbu", col2_inst, col3_details);
                write_reg(sim, rd, (uint32_t)data); // Zero-extend
            }
            else if (funct3 == 0b101)
            { // lhu (CORRIGIDO)
                const uint16_t data = (uint16_t)sim->mem[mem_addr] |
                                      (uint16_t)(sim->mem[mem_addr + 1] << 8);
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rd], imm_i, sim->x_label[rs1]);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", sim->x_label[rd], address, data);
                print_log(sim, "lhu", col2_inst, col3_details);
                write_reg(sim, rd, (uint32_t)data); // Zero-extend
            }
            break;
        }

        // --- Tipo S (Stores) ---
        case 0b0100011:
        {
            const int32_t simm = get_simm_s(imm_s);
            const uint32_t address = sim->x[rs1] + simm;
            const uint32_t mem_addr = address - sim->offset;

            if (funct3 == 0b000)
            { // sb
                const uint8_t data_change = sim->x[rs2] & 0xFF;
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rs2], imm_s & 0xFFF, sim->x_label[rs1]);
                sprintf(col3_details, "mem[0x%08x]=0x%02x", address, data_change);
                print_log(sim, "sb", col2_inst, col3_details);
                sim->mem[mem_addr] = data_change; // Seguro
            }
            else if (funct3 == 0b001)
            { // sh (CORRIGIDO)
                const uint16_t data_change = sim->x[rs2] & 0xFFFF;
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rs2], imm_s & 0xFFF, sim->x_label[rs1]);
                sprintf(col3_details, "mem[0x%08x]=0x%04x", address, data_change);
                print_log(sim, "sh", col2_inst, col3_details);
                sim->mem[mem_addr] = data_change & 0xFF;
                sim->mem[mem_addr + 1] = (data_change >> 8) & 0xFF;
            }
            else if (funct3 == 0b010)
            { // sw (CORRIGIDO)
                const uint32_t data_change = sim->x[rs2];
                sprintf(col2_inst, "%s,0x%03x(%s)", sim->x_label[rs2], imm_s & 0xFFF, sim->x_label[rs1]);
                sprintf(col3_details, "mem[0x%08x]=0x%08x", address, data_change);
                print_log(sim, "sw", col2_inst, col3_details);
                sim->mem[mem_addr] = data_change & 0xFF;
                sim->mem[mem_addr + 1] = (data_change >> 8) & 0xFF;
                sim->mem[mem_addr + 2] = (data_change >> 16) & 0xFF;
                sim->mem[mem_addr + 3] = (data_change >> 24) & 0xFF;
            }
            break;
        }

        // --- Tipo B (Branches) ---
        case 0b1100011:
        {
            const int32_t simm = get_simm_b(imm_b);
            const uint32_t address = sim->pc + simm;
            int condition = 0;

            if (funct3 == 0b000)
            { // beq
                condition = (sim->x[rs1] == sim->x[rs2]);
                sprintf(col3_details, "(0x%08x==0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "beq", col2_inst, col3_details);
            }
            else if (funct3 == 0b001)
            { // bne
                condition = (sim->x[rs1] != sim->x[rs2]);
                sprintf(col3_details, "(0x%08x!=0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "bne", col2_inst, col3_details);
            }
            else if (funct3 == 0b100)
            { // blt
                condition = ((int32_t)sim->x[rs1] < (int32_t)sim->x[rs2]);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "blt", col2_inst, col3_details);
            }
            else if (funct3 == 0b101)
            { // bge
                condition = ((int32_t)sim->x[rs1] >= (int32_t)sim->x[rs2]);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "bge", col2_inst, col3_details);
            }
            else if (funct3 == 0b110)
            { // bltu
                condition = (sim->x[rs1] < sim->x[rs2]);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "bltu", col2_inst, col3_details);
            }
            else if (funct3 == 0b111)
            { // bgeu
                condition = (sim->x[rs1] >= sim->x[rs2]);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", sim->x[rs1], sim->x[rs2], condition, condition ? address : sim->pc + 4);
                print_log(sim, "bgeu", col2_inst, col3_details);
            }

            sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rs1], sim->x_label[rs2], imm_b >> 1);

            if (condition)
            {
                sim->pc = address - 4; // Subtrai 4 para anular o pc += 4 no fim do loop
            }
            break;
        }

        // --- Tipo U ---
        case 0b0110111:
        { // lui
            const int32_t imm_u_val = get_imm_u(instruction);
            const uint32_t data = (uint32_t)imm_u_val;
            sprintf(col2_inst, "%s,0x%05x", sim->x_label[rd], (uint32_t)imm_u_val >> 12);
            sprintf(col3_details, "%s=0x%08x", sim->x_label[rd], data);
            print_log(sim, "lui", col2_inst, col3_details);
            write_reg(sim, rd, data);
            break;
        }
        case 0b0010111:
        { // auipc
            const int32_t imm_u_val = get_imm_u(instruction);
            const uint32_t data = sim->pc + imm_u_val;
            sprintf(col2_inst, "%s,0x%05x", sim->x_label[rd], (uint32_t)imm_u_val >> 12);
            sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", sim->x_label[rd], sim->pc, imm_u_val, data);
            print_log(sim, "auipc", col2_inst, col3_details);
            write_reg(sim, rd, data);
            break;
        }

        // --- Tipo J ---
        case 0b1101111:
        { // jal
            const int32_t simm = get_simm_j(instruction);
            const uint32_t address = sim->pc + simm;
            const uint32_t link_data = sim->pc + 4;

            sprintf(col2_inst, "%s,0x%05x", sim->x_label[rd], (simm >> 1) & 0xFFFFF);
            sprintf(col3_details, "pc=0x%08x,%s=0x%08x", address, sim->x_label[rd], link_data);
            print_log(sim, "jal", col2_inst, col3_details);

            write_reg(sim, rd, link_data);
            sim->pc = address - 4; // Anula o pc += 4
            break;
        }

        // --- Tipo I (JALR) ---
        case 0b1100111:
        { // jalr
            const int32_t simm = get_simm_i(imm_i);
            const uint32_t address = (sim->x[rs1] + simm) & ~1u; // Zera o LSB
            const uint32_t link_data = sim->pc + 4;

            sprintf(col2_inst, "%s,%s,0x%03x", sim->x_label[rd], sim->x_label[rs1], imm_i);
            sprintf(col3_details, "pc=0x%08x+0x%08x,%s=0x%08x", address, simm, sim->x_label[rd], link_data);
            print_log(sim, "jalr", col2_inst, col3_details);

            write_reg(sim, rd, link_data);
            sim->pc = address - 4; // Anula o pc += 4
            break;
        }

        // --- Sistema (EBREAK) ---
        case 0b1110011:
        {
            if (funct3 == 0b000 && imm_i == 1)
            { // ebreak
                fprintf(sim->output, "0x%08x:ebreak\n", sim->pc);
                
                // Leitura segura da instrução anterior e próxima para checagem
                 const uint32_t mem_addr_prev = (sim->pc - 4 - sim->offset);
                 const uint32_t previous = (uint32_t)sim->mem[mem_addr_prev] |
                                           (uint32_t)(sim->mem[mem_addr_prev + 1] << 8) |
                                           (uint32_t)(sim->mem[mem_addr_prev + 2] << 16) |
                                           (uint32_t)(sim->mem[mem_addr_prev + 3] << 24);

                 const uint32_t mem_addr_next = (sim->pc + 4 - sim->offset);
                 const uint32_t next = (uint32_t)sim->mem[mem_addr_next] |
                                       (uint32_t)(sim->mem[mem_addr_next + 1] << 8) |
                                       (uint32_t)(sim->mem[mem_addr_next + 2] << 16) |
                                       (uint32_t)(sim->mem[mem_addr_next + 3] << 24);

                // Condição de parada específica
                if (previous == 0x01f01013 && next == 0x40705013)
                {
                    sim->run = 0;
                }
            }
            break;
        }

        default:
            printf("error: unknown instruction opcode at pc = 0x%08x\n", sim->pc);
            sim->run = 0;
        }

        sim->pc += 4; // Avança para a próxima instrução
    }
}

// --- Função Main ---

int main(int argc, char *argv[])
{
    printf("--------------------------------------------------------------------------------\n");
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <arquivo_hex_entrada> <arquivo_log_saida>\n", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[1], "r");
    if (!input)
    {
        perror("Erro ao abrir arquivo de entrada");
        return 128;
    }

    FILE *output = fopen(argv[2], "w");
    if (!output)
    {
        perror("Erro ao abrir arquivo de saída");
        fclose(input);
        return 128;
    }

    // 1. Inicializar o Simulador
    Simulator sim = {0}; // Inicializa registradores e PC para 0
    sim.offset = 0x80000000;
    sim.pc = sim.offset;
    sim.output = output;
    sim.x_label[0] = "zero";
    sim.x_label[1] = "ra";
    sim.x_label[2] = "sp";
    sim.x_label[3] = "gp";
    sim.x_label[4] = "tp";
    sim.x_label[5] = "t0";
    sim.x_label[6] = "t1";
    sim.x_label[7] = "t2";
    sim.x_label[8] = "s0";
    sim.x_label[9] = "s1";
    sim.x_label[10] = "a0";
    sim.x_label[11] = "a1";
    sim.x_label[12] = "a2";
    sim.x_label[13] = "a3";
    sim.x_label[14] = "a4";
    sim.x_label[15] = "a5";
    sim.x_label[16] = "a6";
    sim.x_label[17] = "a7";
    sim.x_label[18] = "s2";
    sim.x_label[19] = "s3";
    sim.x_label[20] = "s4";
    sim.x_label[21] = "s5";
    sim.x_label[22] = "s6";
    sim.x_label[23] = "s7";
    sim.x_label[24] = "s8";
    sim.x_label[25] = "s9";
    sim.x_label[26] = "s10";
    sim.x_label[27] = "s11";
    sim.x_label[28] = "t3";
    sim.x_label[29] = "t4";
    sim.x_label[30] = "t5";
    sim.x_label[31] = "t6";

    // Use calloc para garantir que a memória comece zerada
    sim.mem = (uint8_t *)calloc(MEM_SIZE_BYTES, 1);
    if (!sim.mem)
    {
        perror("Erro ao alocar memória");
        fclose(input);
        fclose(output);
        return 1;
    }

    // 2. Carregar o Programa
    load_memory(&sim, input);

    // 3. Executar o Programa
    simulate(&sim);

    // 4. Limpeza
    free(sim.mem);
    fclose(output);
    fclose(input);
    printf("--------------------------------------------------------------------------------\n");
    return 0;
}