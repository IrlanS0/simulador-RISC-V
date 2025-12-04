#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MEM_SIZE (32 * 1024)
#define OFFSET_RAM 0x80000000
#define MAX_RAM (MEM_SIZE + OFFSET_RAM)
#define OFFSET_UART 0x10000000
#define MAX_UART 0x10000005
#define OFFSET_CLINT 0x02000000
#define MAX_CLINT 0x020c0000
#define OFFSET_PLIC 0x0c000000
#define MAX_PLIC 0x0c200004
#define OFFSET_MTIMECMP (OFFSET_CLINT + 0x4000)
#define OFFSET_MTIME (OFFSET_CLINT + 0xbff8)

typedef struct
{
    // Registros de controle(CSRs)
    // Registradores de uso específico
    uint32_t mstatus;
    uint32_t mie;
    uint32_t mtvec;
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t mip;
} CSR_REG;

uint32_t csr_r(uint32_t add, CSR_REG *csr)
{
    switch (add)
    {
    case 0x300:
        return csr->mstatus;
    case 0x304:
        return csr->mie;
    case 0x305:
        return csr->mtvec;
    case 0x341:
        return csr->mepc;
    case 0x342:
        return csr->mcause;
    case 0x343:
        return csr->mtval;
    case 0x344:
        return csr->mip;
    default:
        return 0;
    }
};

void csr_w(CSR_REG *csr, uint32_t add, uint32_t data)
{
    switch (add)
    {
    case 0x300:
        csr->mstatus = data;
        break;
    case 0x304:
        csr->mie = data;
        break;
    case 0x305:
        csr->mtvec = data;
        break;
    case 0x341:
        csr->mepc = data;
        break;
    case 0x342:
        csr->mcause = data;
        break;
    case 0x343:
        csr->mtval = data;
        break;
    case 0x344:
        csr->mip = data;
        break;
    default:
        break;
    }
};

void exception_handler(CSR_REG *csr, uint32_t *pc, uint32_t cause,
                       uint32_t tval, FILE *log)
{
    // Atualizando MCAUSE
    csr_w(csr, 0x342, cause);
    // Atualiza o valor de trap(endereco invalido ou instrucao ilegal)
    csr_w(csr, 0x343, tval);
    // Salvar o PC na MEPC
    csr_w(csr, 0x341, *pc);
    // Desativando interrupcoes (MIE = 0)
    uint32_t mstatus = csr_r(0x300, csr);
    // Pega o valor MIE(bit 3)
    uint32_t mie = (mstatus >> 3) & 1;
    // Escreve o valor MIE em MPIE(bit 7)
    mstatus = (mstatus & ~(1 << 7)) | (mie << 7);
    // Zera o bit MIE(Desabilita interrupcoes globais)
    mstatus &= ~(1 << 3);
    mstatus |= (0b11 << 11);

    // Escreve o valor MIE no MSTATUS
    csr_w(csr, 0x300, mstatus);
    // Le o endereco de destino no mtvec (0x305)
    uint32_t vetor = csr_r(0x305, csr);
    // Desviando fluxo: pc recebe o valor de mtvec
    *pc = (vetor & ~0x3);
}

void interruption_handler(CSR_REG *csr, uint32_t *pc, uint32_t cause,
                          uint32_t tval, FILE *log)
{
    // MEPC recebe o codigo da proxima instrucao
    csr_w(csr, 0x341, *pc);
    // Atualizando MCAUSE (bit 31 = 1 para interrupcao)
    csr_w(csr, 0x342, cause | (1U << 31));

    uint32_t mstatus = csr_r(0x300, csr);
    uint32_t mie = (mstatus >> 3) & 1;

    mstatus = (mstatus & ~(1 << 7)) | (mie << 7);
    mstatus &= ~(1 << 3);
    mstatus |= (0b11 << 11);
    csr_w(csr, 0x300, mstatus);

    uint32_t mtvec = csr_r(0x305, csr);
    uint32_t modo = mtvec & 0x3;
    uint32_t base = mtvec & ~0x3;

    if (modo == 1)
    {
        *pc = base + (cause * 4); // Direct(modo 1)
    }
    else
    {
        *pc = base; // Direct(modo 0)
    }
}

uint32_t load(uint8_t *mem, uint32_t addr, uint64_t mtime, uint64_t mtimecmp,
              uint32_t msip)
{
    // 1. SOFTWARE
    if (addr == OFFSET_CLINT)
    {
        return msip;
    }

    // 2. TIMER
    if (addr == OFFSET_MTIME)
    {
        return (uint32_t)(mtime & 0xFFFFFFFF);
    }
    else if (addr == OFFSET_MTIME + 4)
    {
        return (uint32_t)(mtime >> 32);
    }
    else if (addr == OFFSET_MTIMECMP)
    {
        return (uint32_t)(mtimecmp & 0xFFFFFFFF);
    }
    else if (addr == OFFSET_MTIMECMP + 4)
    {
        return (uint32_t)(mtimecmp >> 32);
    }

    // 3. RAM
    uint32_t index = addr - OFFSET_RAM;
    uint32_t word = (uint32_t)mem[index] | (uint32_t)(mem[index + 1] << 8) |
                    (uint32_t)(mem[index + 2] << 16) |
                    (uint32_t)(mem[index + 3] << 24);
    return word;
}

void store(uint8_t *mem, uint32_t addr, uint32_t data, uint64_t *mtimecmp,
           uint32_t *msip)
{
    // 1. SOFTWARE
    if (addr == OFFSET_CLINT)
    {
        *msip = (data & 1);
        return;
    }

    // 2. TIMER
    if (addr == OFFSET_MTIMECMP)
    {
        *mtimecmp = (*mtimecmp & 0xFFFFFFFF00000000) | (uint64_t)data;
        return;
    }
    else if (addr == OFFSET_MTIMECMP + 4)
    {
        *mtimecmp = (*mtimecmp & 0x00000000FFFFFFFF) | ((uint64_t)data << 32);
        return;
    }

    // 3. RAM
    if (addr >= OFFSET_RAM && addr < MAX_RAM)
    {
        uint32_t index = (addr - OFFSET_RAM) >> 2;
        ((uint32_t *)(mem))[index] = data;
    }
}

const char *csr_get_name(uint32_t addr)
{
    switch (addr)
    {
    case 0x300:
        return "mstatus";
    case 0x304:
        return "mie";
    case 0x305:
        return "mtvec";
    case 0x341:
        return "mepc";
    case 0x342:
        return "mcause";
    case 0x343:
        return "mtval";
    case 0x344:
        return "mip";
    default:
        return "csr";
    }
}

void print(uint8_t i, uint32_t pc, uint32_t tval, uint32_t cause,
           char *exc_name, FILE *output)
{
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];
    sprintf(col2_inst, "");
    sprintf(col3_details, "cause=0x%08x,epc=0x%08x,tval=0x%08x", cause, pc, tval);
    if (i == 0)
    {
        // --- EXCEÇÃO ---
        sprintf(col1_addr, ">exception:%s", exc_name);
        if (strcmp(exc_name, "store_fault") == 0)
        {
            fprintf(output, "%-18s%-16s%s\n", col1_addr, col2_inst, col3_details);
        }
        else if (strcmp(exc_name, "environment_call") == 0)
        {
            fprintf(output, "%-18s%-11s%s\n", col1_addr, col2_inst, col3_details);
        }
        else if (strcmp(exc_name, "illegal_instruction") == 0)
        {
            fprintf(output, "%-15s%-8s%s\n", col1_addr, col2_inst, col3_details);
        }
        else if (strcmp(exc_name, "instruction_fault") == 0)
        {
            fprintf(output, "%-15s%-10s%s\n", col1_addr, col2_inst, col3_details);
        }
    }
    else
    {
        // --- INTERRUPÇÃO ---
        sprintf(col1_addr, ">interrupt:%s", exc_name);
        if (strcmp(exc_name, "timer") == 0)
        {
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
        }
        else if (strcmp(exc_name, "software") == 0)
        {
            fprintf(output, "%-18s%-19s%s\n", col1_addr, col2_inst, col3_details);
        }
    }
}

void print_instruction(char *buffer, char *especific_instruction, char *col1_addr, char *col2_inst, uint32_t pc,
                        uint32_t rs1, uint32_t rs2, uint32_t rd, uint32_t imm,
                        char *instruction_name, char **x_label)
{
    char tipo = tolower(buffer[0]);
    sprintf(col1_addr, "0x%08x:%s", pc, instruction_name);
    if (tipo == 'r'){
        sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
    } 
    else if (tipo == 'i'){
        if(strcmp(especific_instruction, "print_imm") == 0)
        {
            sprintf(col2_inst, "%s,%s,%u", x_label[rd], x_label[rs1], imm);
        }
        else if (strcmp(especific_instruction, "load") == 0)
        {
            sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rd], (imm & 0xFFF), x_label[rs1]);
        }
        else
        {
            sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm & 0xFFF);
        }
    }
    else if (tipo == 'c'){
        if (strcmp(especific_instruction, "nind") == 0){
            sprintf(col2_inst, "");
        }
    }
    else if (tipo == 's'){
        sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rs2], (uint16_t)(imm & 0xFFF), x_label[rs1]);
    }
    else if (tipo == 'b'){
        sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], imm >> 1);
    }
}

int main(int argc, char *argv[])
{
    printf("---------------------------------------------------------------------"
            "-----------\n");
    FILE *input = fopen(argv[1], "r");
    if (!input)
    {
        perror("Erro ao abrir arquivo de entrada");
        exit(128);
    };
    FILE *output = fopen(argv[2], "w");
    if (!output)
    {
        perror("Erro ao abrir arquivo de saida");
        fclose(input);
        exit(128);
    };
    FILE *log = fopen("log.txt", "w");
    if (!log)
    {
        perror("Erro ao abrir arquivo de log");
        fclose(input);
        fclose(output);
        exit(128);
    };

    const uint32_t offset = 0x80000000;
    // declarando registradores
    const char *x_label[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0",
        "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
        "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

    uint8_t *mem = (uint8_t *)calloc(1, MEM_SIZE);
    if (!mem)
    {
        perror("Erro ao alocar memória para memória");
        fclose(input);
        fclose(output);
        fclose(log);
        return 1;
    }

    char token[32];
    size_t i = 0;
    while (fscanf(input, "%31s", token) == 1)
    {
        if (token[0] == '@')
        {
            continue;
        }
        unsigned int valor;
        if (sscanf(token, "%x", &valor) == 1)
        {
            if (i < MEM_SIZE)
            {
                mem[i++] = (uint8_t)valor;
            }
        }
    }

    // Inicializando variáveis
    CSR_REG csr = {0};
    uint8_t run = 1;
    uint32_t pc = offset;
    uint32_t x[32] = {0};
    uint32_t msip = 0;
    uint64_t mtime = 0, mtimecmp = -1;
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];
    char especific_instruction[128];

    while (run)
    {
        // Incrementa o timer
        mtime++;
        if (mtime >= mtimecmp)
            csr.mip |= (1 << 7);
        else
            csr.mip &= ~(1 << 7);

        // Verifica bit de interrupção de software
        if (msip & 1)
            csr.mip |= (1 << 3);
        else
            csr.mip &= ~(1 << 3);

        uint32_t mstatus = csr_r(0x300, &csr);
        uint32_t mie = csr_r(0x304, &csr);
        uint32_t mip = csr.mip;
        bool global_enable = (mstatus >> 3) & 1;

        // Verifica se há interrupção de software
        bool soft_enable = (mie >> 3) & 1;
        bool soft_pending = (mip >> 3) & 1;
        bool soft_interrupt = global_enable && soft_enable && soft_pending;
        if (soft_interrupt)
        {
            interruption_handler(&csr, &pc, 3, 0, log);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(1, mepc, mtval, mcause, "software", output);
            continue;
        }

        // Verifica se há interrupção de timer
        bool timer_enable = (mie >> 7) & 1;
        bool timer_pending = (mip >> 7) & 1;
        bool timer_interrupt = global_enable && timer_enable && timer_pending;
        if (timer_interrupt)
        {
            interruption_handler(&csr, &pc, 7, 0, log);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(1, mepc, mtval, mcause, "timer", output);
            continue;
        }

        // Verifica se há exceção de instrução inválida
        if (pc >= MAX_RAM || pc < offset)
        {
            exception_handler(&csr, &pc, 1, pc, log);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            print(0, mepc, mtval, mcause, "instruction_fault", output);
            continue;
        }

        // Fetch
        const uint32_t instruction = ((uint32_t *)(mem))[(pc - offset) >> 2];
        const uint8_t opcode = instruction & 0b1111111;
        const uint8_t funct7 = instruction >> 25;
        const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
        // Imediato de 12 e 7 bits, tipos b e s, respectivamente 
        const uint16_t imm = (instruction >> 20) & 0b111111111111;
        const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
        const uint16_t imm_b = ((instruction >> 31) & 0b1) << 12 |
                                ((instruction >> 7) & 0b1) << 11 |
                                ((instruction >> 25) & 0b111111) << 5 |
                                ((instruction >> 8) & 0b1111) << 1;
        const uint16_t imm_s =
            ((instruction >> 25) & 0b1111111) << 5 | ((instruction >> 7) & 0b11111);
        // Registradores de uso geral e uso especifico
        const uint8_t rs1 = (instruction & (0b11111 << 15)) >> 15;
        const uint8_t rs2 = (instruction & (0b11111 << 20)) >> 20;
        const uint8_t rd = (instruction & (0b11111 << 7)) >> 7;
        const uint32_t csr_address = (instruction >> 20) & 0xFFF;

        switch (opcode)
        {
        // tipo R
        case 0b0110011:
        {
            char *buffer_type = "R";
            if (funct3 == 0b000 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] + x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "add",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b000 && funct7 == 0b0100000)
            {
                const int32_t data = (int32_t)x[rs1] - (int32_t)x[rs2];
                if (rd != 0)
                    x[rd] = (uint32_t)data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sub",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x-0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b001 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] << x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sll",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x<<0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "srl",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sra",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>>0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b010 && funct7 == 0b0000000)
            {
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "slt",
                                  (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b011 && funct7 == 0b0000000)
            {
                const uint32_t data = (x[rs1] < x[rs2]) ? 1 : 0;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sltu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b110 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] | x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "or",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b111 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] & x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "and",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b100 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] ^ x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "xor",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b000 && funct7 == 0b0000001)
            {
                const int64_t data = (int32_t)x[rs1] * (int32_t)x[rs2];
                const int32_t data_low = (int32_t)(data);
                if (rd != 0)
                    x[rd] = (uint32_t)data_low;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mul",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_low);
            }
            else if (funct3 == 0b001 && funct7 == 0b0000001)
            {
                int64_t data = (int64_t)(int32_t)x[rs1] * (int64_t)(int32_t)x[rs2];
                uint32_t data_high = (uint32_t)(data >> 32);
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulh",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
            }
            else if (funct3 == 0b010 && funct7 == 0b0000001)
            {
                const int64_t data =
                    (int64_t)(int32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                const int32_t data_high = (int32_t)(data >> 32);
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulhsu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
            }
            else if (funct3 == 0b011 && funct7 == 0b0000001)
            {
                const uint64_t data =
                    (int64_t)(uint32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                int32_t data_high = (uint32_t)(data >> 32);
                if (rd != 0)
                    x[rd] = data_high;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulhu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
            }
            else if (funct3 == 0b100 && funct7 == 0b0000001)
            {
                const uint32_t data =
                    (x[rs2] == 0) ? 0xFFFFFFFF
                                    : (uint32_t)((int32_t)x[rs1] / (int32_t)x[rs2]);
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "div",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b101 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : x[rs1] / x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "divu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b110 && funct7 == 0b0000001)
            {
                const uint32_t data =
                    (x[rs2] == 0) ? x[rs1]
                                    : (uint32_t)((int32_t)x[rs1] % (int32_t)x[rs2]);
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "rem",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
            else if (funct3 == 0b111 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? x[rs1] : x[rs1] % x[rs2];
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "remu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
            }
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        // tipo I
        case 0b0010011:
        {
            uint32_t rs1_antigo = x[rs1];
            char *buffer_type = "I"; 
            const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
            
            if (funct3 == 0b001)
            {
                const uint32_t data = x[rs1] << uimm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "slli", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x<<%u=0x%08x", x_label[rd], rs1_antigo, uimm,
                        data);
            }
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> uimm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "srli", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", x_label[rd], rs1_antigo, uimm,
                        data);
            }
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> (int32_t)uimm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "srai", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>>%u=0x%08x", x_label[rd], rs1_antigo,
                        uimm, data);
            }
            else if (funct3 == 0b010)
            {
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)simm) ? 1 : 0;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "slti", (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], rs1_antigo,
                        simm, data);
            }
            else if (funct3 == 0b011)
            {
                const uint32_t data = (x[rs1] < simm) ? 1 : 0;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sltiu", (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], rs1_antigo,
                        simm, data);
            }
            else if (funct3 == 0b100)
            {
                const uint32_t data = x[rs1] ^ simm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "xori", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        imm, data);
            }
            else if (funct3 == 0b111)
            {
                const uint32_t data = x[rs1] & simm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "andi", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        imm, data);
            }
            else if (funct3 == 0b110)
            {
                const uint32_t data = x[rs1] | simm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "ori", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        imm, data);
            }
            else if (funct3 == 0b000)
            {
                const uint32_t data = x[rs1] + (int32_t)simm;
                if (rd != 0)
                    x[rd] = data;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "addi", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        imm, data);
            }

            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        case 0b1100111:
        {
            if (funct3 == 0b000)
            {
                char *buffer_type = "I";
                const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t address = (x[rs1] + (int32_t)simm) & ~1u;
                if (rd != 0)
                    x[rd] = pc + 4;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "jalr", (char **)x_label);
                sprintf(col3_details, "pc=0x%08x+0x%08x,%s=0x%08x", address, simm,
                        x_label[rd], pc + 4);
                pc = address - 4;
            }
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        // tipo I
        case 0b0000011:
        {
            const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
            const uint32_t address = x[rs1] + simm;
            char *buffer_type = "I"; 
            
            if (funct3 == 0b000)
            {
                if (address >= MAX_RAM || address < OFFSET_RAM)
                {
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }

                const int8_t data = *((int8_t *)(mem + address - offset));
                if (rd != 0)
                    x[rd] = (int32_t)data;
                
                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lb", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
            }
            else if (funct3 == 0b100)
            {
                if (address >= MAX_RAM || address < OFFSET_RAM)
                {
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }

                const uint8_t data = *((uint8_t *)(mem + address - offset));
                if (rd != 0)
                    x[rd] = (uint8_t)data;
                
                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lbu", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, (uint8_t)data);
            }
            else if (funct3 == 0b001)
            {
                if (address + 2 >= MAX_RAM || address < OFFSET_RAM)
                {
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }

                const int16_t data = *((uint16_t *)(mem + address - offset));
                if (rd != 0)
                    x[rd] = (int32_t)data;

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lh", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
            }
            else if (funct3 == 0b101) 
            {
                if (address + 2 >= MAX_RAM || address < OFFSET_RAM)
                {
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }

                const uint16_t data = *((uint16_t *)(mem + address - offset));
                if (rd != 0)
                    x[rd] = (uint32_t)data;
                
                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "lhu", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
            }
            else if (funct3 == 0b010) 
            {
                bool RAM = (address >= OFFSET_RAM && address <= MAX_RAM);
                bool CLINT = (address >= OFFSET_CLINT && address <= MAX_CLINT);
                if (!RAM && !CLINT)
                {
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }
                const uint32_t data = load(mem, address, mtime, mtimecmp, msip);
                if (rd != 0)
                    x[rd] = data;
                
                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lw", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
                }
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        case 0b1110011:
        {
            // Chamei de tipo C apenas para impressao
            char *buffer_type = "C";
            // Pega o registrador CSR e constroi o operando
            uint32_t csr_antigo = csr_r(csr_address, &csr);
            uint32_t operando;
            if (funct3 & 0b100) 
                // imediato
                operando = (uint32_t)rs1;
            else
                operando = x[rs1];

            // EBREAK
            if (funct3 == 0b000 && imm == 1)
            {
                print_instruction(buffer_type, "nind", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "ebreak", (char **)x_label);
                sprintf(col3_details, "");
                const uint32_t previous = ((uint32_t *)(mem))[(pc - 4 - offset) >> 2];
                const uint32_t next = ((uint32_t *)(mem))[(pc + 4 - offset) >> 2];
                if (previous == 0x01f01013 && next == 0x40705013)
                    run = 0;
            }
            // ECALL
            else if (imm == 0)
            {
                print_instruction(buffer_type, "nind", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "ecall", (char **)x_label);
                sprintf(col3_details, "");
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                print(0, pc, 0, 11, "environment_call", output);
                exception_handler(&csr, &pc, 11, 0, log);
                continue;
            }
            // MRET
            else if (instruction == 0x30200073)
            {
                uint32_t mstatus = csr_r(0x300, &csr);
                // Pega o valor MPIE(bit 7)
                uint32_t current_mpie = (mstatus >> 7) & 1;
                // Escreve o valor MPIE em MIE(bit 3)
                mstatus = (mstatus & ~(1 << 3)) | (current_mpie << 3);
                // Habilita o bit MPIE e aceita interrupcoes globais
                mstatus |= (1 << 7);
                // Escreve o valor MPIE no MSTATUS
                csr_w(&csr, 0x300, mstatus);
                // Le o endereco de retorno na MEPC (0x341)
                print_instruction(buffer_type, "nind", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mret", (char **)x_label);
                sprintf(col3_details, "pc=0x%08x", csr_r(0x341, &csr));
                pc = csr_r(0x341, &csr) - 4;
            }

            uint32_t new_data = csr_antigo;
            if ((funct3 & 0b011) == 0b001)
            {
                new_data = operando;
                csr_w(&csr, csr_address, new_data);

                if (funct3 == 0b001)
                { 
                    sprintf(col1_addr, "0x%08x:csrrw", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address),
                            x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s=%s=0x%08x", x_label[rd],
                            csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), x_label[rs1], new_data);
                }
                else
                { 
                    sprintf(col1_addr, "0x%08x:csrrwi", pc);
                    sprintf(col2_inst, "%s,%s,0x%05x", x_label[rd],
                            csr_get_name(csr_address), operando);
                    sprintf(col3_details, "%s=%s=0x%08x,%s=0x%02x=0x%08x", x_label[rd],
                            csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), operando, new_data);
                }
            }
            else if ((funct3 & 0b011) == 0b010)
            {
                if (rs1 != 0)
                {
                    new_data = csr_antigo | operando;
                    csr_w(&csr, csr_address, new_data);
                }

                if (funct3 == 0b010)
                { 
                    sprintf(col1_addr, "0x%08x:csrrs", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address),
                            x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s|=%s=0x%08x|0x%08x=0x%08x",
                            x_label[rd], csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), x_label[rs1], csr_antigo, operando,
                            new_data);
                }
                else
                { 
                    sprintf(col1_addr, "0x%08x:csrrsi", pc);
                    sprintf(col2_inst, "%s,%s,0x%05x", x_label[rd],
                            csr_get_name(csr_address), operando);
                    sprintf(col3_details, "%s=%s=0x%08x,%s=0x%02x=0x%08x", x_label[rd],
                            csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), operando, new_data);
                }
            }
            else if ((funct3 & 0b011) == 0b011)
            {
                if (rs1 != 0)
                {
                    new_data = csr_antigo & (~operando);
                    csr_w(&csr, csr_address, new_data);
                }

                if (funct3 == 0b011)
                { 
                    sprintf(col1_addr, "0x%08x:csrrc", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address),
                            x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s&~=%s=0x%08x&~0x%08x=0x%08x",
                            x_label[rd], csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), x_label[rs1], csr_antigo, operando,
                            new_data);
                }
                else
                { 
                    sprintf(col1_addr, "0x%08x:csrrci", pc);
                    sprintf(col2_inst, "%s,%s,0x%05x", x_label[rd],
                            csr_get_name(csr_address), operando);
                    sprintf(col3_details,
                            "%s=%s=0x%08x,%s&~=0x%02x=0x%08x&~0x%08x=0x%08x", x_label[rd],
                            csr_get_name(csr_address), csr_antigo,
                            csr_get_name(csr_address), operando, csr_antigo, operando,
                            new_data);
                }
            }
            
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            if (funct3 != 0 && rd != 0)
                x[rd] = csr_antigo;

            break;
        }
        // tipo S
        case 0b0100011:
        {
            char *buffer_type = "S";
            const uint32_t simm = (imm_s >> 11) ? (0xFFFFF000 | imm_s) : (imm_s);
            const uint32_t address = x[rs1] + simm;
            uint32_t data_change = x[rs2];

            if (funct3 == 0b000)
            {
                *((uint8_t *)(mem + address - offset)) = (uint8_t)data_change;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sb", (char **)x_label);
                sprintf(col3_details, "mem[0x%08x]=0x%02x", address, data_change);
            }
            else if (funct3 == 0b001)
            {
                if (address + 2 > MAX_RAM || address < OFFSET_RAM)
                {
                    print(0, pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address, log);
                    continue;
                }

                *((uint16_t *)(mem + address - offset)) = (uint16_t)data_change;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sh", (char **)x_label);
                sprintf(col3_details, "mem[0x%08x]=0x%04x", address, data_change);
            }
            else if (funct3 == 0b010) 
            {
                bool RAM = (address >= OFFSET_RAM && address + 4 <= MAX_RAM);
                bool CLINT = (address >= OFFSET_CLINT && address <= MAX_CLINT);
                if (!RAM && !CLINT)
                {
                    print(0, pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address, log);
                    continue;
                }
                store(mem, address, data_change, &mtimecmp, &msip);

                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sw", (char **)x_label);
                sprintf(col3_details, "mem[0x%08x]=0x%08x", address, data_change);
            }

            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        // tipo B
        case 0b1100011:
        {
            const int32_t simm = (imm_b & 0x1000) ? (imm_b | 0xFFFFE000) : imm_b;
            const uint32_t address = pc + simm;
            int condition = 0;
            char *buffer_type = "B";
            
            if (funct3 == 0b000)
            {
                condition = (int32_t)x[rs1] == (int32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "beq", (char **)x_label);
                sprintf(col3_details, "(0x%08x==0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }
            else if (funct3 == 0b001)
            {
                condition = (int32_t)x[rs1] != (int32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "bne", (char **)x_label);
                sprintf(col3_details, "(0x%08x!=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }
            else if (funct3 == 0b100)
            {
                condition = (int32_t)x[rs1] < (int32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "blt", (char **)x_label);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }
            else if (funct3 == 0b101)
            {
                condition = (int32_t)x[rs1] >= (int32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "bge", (char **)x_label);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }
            else if (funct3 == 0b110)
            {
                condition = (uint32_t)x[rs1] < (uint32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "bltu", (char **)x_label);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }
            else if (funct3 == 0b111)
            {
                condition = (uint32_t)x[rs1] >= (uint32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm_b, "bgeu", (char **)x_label);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2],
                        condition, condition ? address : pc + 4);
            }

            // definindo proximo pc
            if (condition)
                pc = address - 4;
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        // tipo U
        case 0b0110111:
        {
            uint32_t imm20_a = instruction >> 12;
            const uint32_t data = imm20_a << 12;
            if (rd != 0)
                x[rd] = data;

            sprintf(col1_addr, "0x%08x:lui", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_a & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x", x_label[rd], data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            break;
        }
        case 0b0010111: // tipo U
        {
            const int32_t imm_u_val = (int32_t)(instruction & 0xFFFFF000);
            const uint32_t data = pc + imm_u_val;
            if (rd != 0)
                x[rd] = data;

            // Para o log, podemos mostrar os 20 bits originais
            const uint32_t imm20_for_log = (uint32_t)imm_u_val >> 12;
            sprintf(col1_addr, "0x%08x:auipc", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_for_log & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], pc,
                    imm_u_val, data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

            break;
        }
        // tipo J
        case 0b1101111:
        {
            const uint32_t imm20_bit = (instruction & 0x80000000) >> 11;
            const uint32_t imm10_1_bits = (instruction & 0x7FE00000) >> 20;
            const uint32_t imm11_bit = (instruction & 0x00100000) >> 9;
            const uint32_t imm19_12_bits = (instruction & 0x000FF000);

            // Juntando os pedaços
            const uint32_t imm_j =
                imm20_bit | imm10_1_bits | imm11_bit | imm19_12_bits;
            const int32_t simm = (imm_j & 0x00100000) ? (imm_j | 0xFFE00000) : imm_j;
            const uint32_t address = pc + simm;

            sprintf(col1_addr, "0x%08x:jal", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], (simm >> 1) & 0xFFFFF);
            sprintf(col3_details, "pc=0x%08x,%s=0x%08x", address, x_label[rd],
                    pc + 4);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

            if (rd != 0)
                x[rd] = pc + 4;

            pc = address - 4;
            break;
        }
        default:
        {
            exception_handler(&csr, &pc, 2, instruction, log);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(0, mepc, mtval, mcause, "illegal_instruction", output);
            continue;
        }
        }
        pc += 4;
    }

    free(mem);
    fclose(output);
    fclose(input);
    fclose(log);
    printf("---------------------------------------------------------------------"
            "-----------\n");
    return 0;
}