#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#define MEM_SIZE (32 * 1024)
#define offset_ram 0x80000000
#define max_ram (MEM_SIZE + offset_ram)
#define offset_uart 0x10000000
#define max_uart 0x10000005
#define offset_clint 0x02000000
#define max_clint 0x020c0000
#define offset_plic 0x0c000000
#define max_plic 0x0c200004
#define offset_mtimecmp (offset_clint + 0x4000)
#define offset_mtime (offset_clint + 0xbff8)

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

void exception_handler(CSR_REG *csr, uint32_t *pc, uint32_t cause, uint32_t tval, FILE *log)
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

void interruption_handler(CSR_REG *csr, uint32_t *pc, uint32_t cause, uint32_t tval, FILE *log) 
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

    if (modo == 1){
        *pc = base + (cause * 4);
    } else {
        *pc = base;
    }
}

uint32_t load(uint8_t *mem, uint32_t addr, uint64_t mtime, uint64_t mtimecmp)
{
    // Primeiramente fiz a logica do CLINT(Aparentemente mais facil)
    if (addr == offset_mtime)
    {
        // 32 menos significativos
        return (uint32_t)(mtime & 0xFFFFFFFF);
    }
    else if (addr == offset_mtime + 4)
    {
        // 32 mais significativos
        return (uint32_t)(mtime >> 32);
    }
    else if (addr == offset_mtimecmp)
    {
        return (uint32_t)(mtimecmp & 0xFFFFFFFF);
    }
    else if (addr == offset_mtimecmp + 4)
    {
        return (uint32_t)(mtimecmp >> 32);
    }

    uint32_t index = addr - offset_ram;
    uint32_t word = (uint32_t)mem[index] |
                    (uint32_t)(mem[index + 1] << 8) |
                    (uint32_t)(mem[index + 2] << 16) |
                    (uint32_t)(mem[index + 3] << 24);
    return word;
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

void print_exception(uint32_t pc, uint32_t tval, uint32_t cause, char *exc_name, FILE *output)
{
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];
    sprintf(col1_addr, ">exception:%s", exc_name);
    sprintf(col2_inst, "");
    sprintf(col3_details, "cause=0x%08x,epc=0x%08x,tval=0x%08x", cause, pc, tval);
    if (strcmp(exc_name, "store_fault") == 0)
    {
        fprintf(output, "%-18s%-16s%s\n", col1_addr, col2_inst, col3_details);
    }
    else if (strcmp(exc_name, "environment_call") == 0)
    {
        fprintf(output, "%-18s%-11s%s\n", col1_addr, col2_inst, col3_details);
    }
    else
    {
        fprintf(output, "%-18s%-17s%s\n", col1_addr, col2_inst, col3_details);
    }
}

int main(int argc, char *argv[])
{
    printf("--------------------------------------------------------------------------------\n");
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

    char token[32];
    const uint32_t offset = 0x80000000;
    size_t i = 0;
    // declarando registradores
    uint32_t x[32] = {0};
    const char *x_label[32] = {"zero", "ra", "sp", "gp",
                               "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2",
                               "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
                               "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

    const char *csr_label[32] = {"mstatus", "mie", "mtvec", "mepc",
                                 "mcause", "mtval", "mip"};

    uint32_t pc = offset;

    uint8_t *mem = (uint8_t *)calloc(1, MEM_SIZE);
    if (!mem)
    {
        perror("Erro ao alocar memória para memória");
        fclose(input);
        fclose(output);
        fclose(log);
        return 1;
    }

    while (fscanf(input, "%31s", token) == 1)
    {
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

    CSR_REG csr = {0};
    uint8_t run = 1;
    uint64_t mtime = 0, mtimecmp = -1;
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];
    while (run)
    {
        // ---- Incrementa o tempo(CLINT) ----
        mtime++;
        if (mtime >= mtimecmp)
        {
            csr.mip |= (1 << 7);
        }
        else
        {
            csr.mip &= ~(1 << 7);
        }

        uint32_t mstatus = csr_r(0x300, &csr);
        uint32_t mip = csr.mip;

        bool timer_interrupt = (mstatus & (1 << 3)) && (mip & (1 << 7));

        if (timer_interrupt) {
            interruption_handler(&csr, &pc, 7, 0, log);
            continue;
        }

        if (pc >= max_ram || pc < offset)
        {
            exception_handler(&csr, &pc, 1, pc, log);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            sprintf(col1_addr, ">exception:instruction_fault");
            sprintf(col2_inst, "");
            sprintf(col3_details, "cause=0x%08x,epc=0x%08x,tval=0x%08x", mcause, mepc, mtval);
            fprintf(output, "%-15s%-10s%s\n", col1_addr, col2_inst, col3_details);
            continue;
        }

        if (pc % 4 != 0)
        {
            exception_handler(&csr, &pc, 0, pc, log);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            sprintf(col1_addr, ">exception:instruction_fault");
            sprintf(col2_inst, "");
            sprintf(col3_details, "cause=0x%08x,epc=0x%08x,tval=0x%08x", mcause, mepc, mtval);
            fprintf(output, "%-15s%-10s%s\n", col1_addr, col2_inst, col3_details);
            continue;
        }

        // alinhando os 4 bytes da memoria
        const uint32_t instruction = ((uint32_t *)(mem))[(pc - offset) >> 2];
        // Opcodes
        const uint8_t opcode = instruction & 0b1111111;
        // funct7
        const uint8_t funct7 = instruction >> 25;
        // Funct3
        const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
        // imediato de 12 bits
        const uint16_t imm = (instruction >> 20) & 0b111111111111;
        // imediato de 7 bits
        const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
        // Imediato tipo b
        const uint16_t b_imm2 = ((instruction >> 31) & 0b1) << 12 |
                                ((instruction >> 7) & 0b1) << 11 |
                                ((instruction >> 25) & 0b111111) << 5 |
                                ((instruction >> 8) & 0b1111) << 1;
        // Imediato tipo s
        const uint16_t s_imm = ((instruction >> 25) & 0b1111111) << 5 |
                               ((instruction >> 7) & 0b11111);
        // registradores de uso geral
        const uint8_t rs1 = (instruction & (0b11111 << 15)) >> 15;
        const uint8_t rs2 = (instruction & (0b11111 << 20)) >> 20;
        const uint8_t rd = (instruction & (0b11111 << 7)) >> 7;
        // Registradores de controle
        const uint32_t csr_address = (instruction >> 20) & 0xFFF;

        switch (opcode)
        {
        // tipo R
        case 0b0110011:
        {
            // add
            if (funct3 == 0b000 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] + x[rs2];
                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:add", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                if (rd != 0)
                    x[rd] = data;
            }
            // sub funct3 == 000 and funct7 == 0100000
            else if (funct3 == 0b000 && funct7 == 0b0100000)
            {
                const int32_t data = (int32_t)x[rs1] - (int32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:sub", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x-0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = (uint32_t)data;
            }
            // sll funct3 == 001 and funct7 == 0000000
            else if (funct3 == 0b001 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] << x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:sll", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x<<0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // srl funct3 == 101 and funct7 == 0000000
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> x[rs2];
                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:srl", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x>>0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // sra funct3 == 101 and funct7 == 0100000
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> x[rs2];
                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:sra", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x>>>0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // slt funct3 == 010 and funct7 == 0000000
            else if (funct3 == 0b010 && funct7 == 0b0000000)
            {
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0;
                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:slt", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // sltu funct3 == 011 and funct7 == 0000000
            else if (funct3 == 0b011 && funct7 == 0b0000000)
            {
                const uint32_t data = (x[rs1] < x[rs2]) ? 1 : 0;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:sltu", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // or fucnt3 == 110 and funct7 == 0000000
            else if (funct3 == 0b110 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] | x[rs2];
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:or", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // and funct3 == 111 and funct7 == 0000000
            else if (funct3 == 0b111 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] & x[rs2];
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:and", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // xor funct3 == 100 and funct7 == 0000000
            else if (funct3 == 0b100 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] ^ x[rs2];
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:xor", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // mul funct3 == 000 and funct7 == 0000001
            else if (funct3 == 0b000 && funct7 == 0b0000001)
            {
                const int64_t data = (int32_t)x[rs1] * (int32_t)x[rs2];
                const int32_t data_low = (int32_t)(data);
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:mul", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data_low);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = (uint32_t)data_low;
            }
            // mulh funct3 == 001 and funct7 == 0000001
            else if (funct3 == 0b001 && funct7 == 0b0000001)
            {
                int64_t data = (int64_t)(int32_t)x[rs1] * (int64_t)(int32_t)x[rs2];
                uint32_t data_high = (uint32_t)(data >> 32);

                sprintf(col1_addr, "0x%08x:mulh", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data_high);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
            }
            // mulhsu funct3 == 010 and funct7 == 0000001
            else if (funct3 == 0b010 && funct7 == 0b0000001)
            {
                const int64_t data = (int64_t)(int32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                const int32_t data_high = (int32_t)(data >> 32);
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:mulhsu", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data_high);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
            }
            // mulhu funct3 == 011 and funct7 == 0000001
            else if (funct3 == 0b011 && funct7 == 0b0000001)
            {
                // const uint64_t data = (uint32_t)x[rs1] * (uint32_t)x[rs2];
                // const uint32_t data_high = (uint32_t)(data >> 32);
                const uint64_t data = (int64_t)(uint32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                int32_t data_high = (uint32_t)(data >> 32);
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:mulhu", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data_high);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data_high;
            }
            // div funct3 == 100 and funct7 == 0000001
            else if (funct3 == 0b100 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : (uint32_t)((int32_t)x[rs1] / (int32_t)x[rs2]);
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:div", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // divu funct3 == 101 and funct7 == 0000001
            else if (funct3 == 0b101 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : x[rs1] / x[rs2];
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:divu", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // rem funct3 == 110 and funct7 == 0000001
            else if (funct3 == 0b110 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? x[rs1] : (uint32_t)((int32_t)x[rs1] % (int32_t)x[rs2]);
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:rem", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // remu funct3 == 111 and funct7 == 0000001
            else if (funct3 == 0b111 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? x[rs1] : x[rs1] % x[rs2];
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:remu", pc);
                sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            break;
        }
        // tipo I
        case 0b0010011:
        {
            // slli (funct3 == 0b001 && funct7 == 0b0000000)
            if (funct3 == 0b001)
            {
                const uint32_t data = x[rs1] << uimm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:slli", pc);
                sprintf(col2_inst, "%s,%s,%u", x_label[rd], x_label[rs1], uimm);
                sprintf(col3_details, "%s=0x%08x<<%u=0x%08x", x_label[rd], x[rs1], uimm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // srli funct3 == 101 and funct7 == 0000000
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> uimm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:srli", pc);
                sprintf(col2_inst, "%s,%s,%u", x_label[rd], x_label[rs1], uimm);
                sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", x_label[rd], x[rs1], uimm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // srai funct3 == 101 and funct7 == 0100000
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> (int32_t)uimm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:srai", pc);
                sprintf(col2_inst, "%s,%s,%u", x_label[rd], x_label[rs1], uimm);
                sprintf(col3_details, "%s=0x%08x>>>%u=0x%08x", x_label[rd], x[rs1], uimm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // slti funct3 == 010 and funct7 == 0000000
            else if (funct3 == 0b010)
            {
                const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)simm) ? 1 : 0;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:slti", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], simm & 0xFFF);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1], simm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // sltiu funct3 == 011 and funct7 == 0000000
            else if (funct3 == 0b011)
            {
                const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t data = (x[rs1] < simm) ? 1 : 0;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:sltiu", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], simm & 0xFFF);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1], simm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // xori funct3 == 100 and funct7 == 0000000
            else if (funct3 == 0b100)
            {
                const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t data = x[rs1] ^ simm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:xori", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], x[rs1], imm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // andi funct3 == 111 and funct7 == 0000000
            else if (funct3 == 0b111)
            {
                const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t data = x[rs1] & simm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:andi", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], x[rs1], imm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // ori funct3 == 110 and funct7 == 0000000
            else if (funct3 == 0b110)
            {
                const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t data = x[rs1] | simm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:ori", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], x[rs1], imm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            // addi funct3 == 000 and funct7 == 0000000
            else if (funct3 == 0b000)
            {
                uint32_t simm = (imm & 0x800) ? (int32_t)(0xFFFFF000 | imm) : (imm);

                const uint32_t data = x[rs1] + (int32_t)simm;
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:addi", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1], simm, data);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualizando registrador se nao for x0
                if (rd != 0)
                    x[rd] = data;
            }
            break;
        }
        case 0b1100111:
        {
            // jalr tipo I
            if (funct3 == 0b000)
            {
                const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                const uint32_t address = (x[rs1] + (int32_t)simm) & ~1u; // o padrao risc-v zera o bit 0
                // imprimindo instrucao no arquivo

                sprintf(col1_addr, "0x%08x:jalr", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                sprintf(col3_details, "pc=0x%08x+0x%08x,%s=0x%08x", address, simm, x_label[rd], pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // atualiza registrador se nao for x0
                if (rd != 0)
                    x[rd] = pc + 4;
                pc = address - 4; // subtraindo 4 pois o pc sera incrementado apos o switch
            }
            break;
        }
        // tipo I
        case 0b0000011:
        {
            const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
            const uint32_t address = x[rs1] + simm;
            // lb funct3 == 000
            if (funct3 == 0b000)
            {
                // Verifica limites de memoria
                if (address >= max_ram || address < offset_ram)
                {
                    // code 5: Load access fault
                    print_exception(pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }
                else
                {
                    const int8_t data = *((int8_t *)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:lb", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rd], (uint16_t)(simm & 0xFFF), x_label[rs1]);
                    sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // atualizando registrador se nao for x0
                    if (rd != 0)
                        x[rd] = (int32_t)data;
                }
            }
            // lbu funct3 == 100
            else if (funct3 == 0b100)
            {
                if (address >= max_ram || address < offset_ram)
                {
                    // code 5: Load access fault
                    print_exception(pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }
                else
                {
                    const uint8_t data = *((uint8_t *)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:lbu", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rd], simm, x_label[rs1]);
                    sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, (uint8_t)data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // atualizando registrador se nao for x0
                    if (rd != 0)
                        x[rd] = (uint8_t)data;
                }
            }
            // lh funct3 == 001
            else if (funct3 == 0b001)
            {
                // Verifica alinhamento do endereco
                if (address % 2 != 0)
                {
                    print_exception(pc, address, 4, "load_fault", output);
                    // code 4: Load address misaligned
                    exception_handler(&csr, &pc, 4, address, log);
                    continue;
                }
                // Verifica limites
                else if (address + 2 >= max_ram || address < offset_ram)
                {
                    // code 5: Load access fault
                    print_exception(pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }
                else
                {
                    // lendo 2 bytes da memoria
                    const int16_t data = *((uint16_t *)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:lh", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rd], simm, x_label[rs1]);
                    sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // atualizando registrador se nao for x0
                    if (rd != 0)
                        x[rd] = (int32_t)data;
                }
            }
            // lhu funct3 == 101
            else if (funct3 == 0b101)
            {
                // Verifica alinhamento do endereco
                if (address % 2 != 0)
                {
                    // code 4: Load address misaligned
                    print_exception(pc, address, 4, "load_fault", output);
                    exception_handler(&csr, &pc, 4, address, log);
                    continue;
                }
                // Verifica limites
                else if (address + 2 >= max_ram || address < offset_ram)
                {
                    // code 5: Load access fault
                    print_exception(pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }
                else
                {
                    // lendo 2 bytes da memoria
                    const uint16_t data = *((uint16_t *)(mem + address - offset));

                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:lhu", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rd], imm, x_label[rs1]);
                    sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // atualizando registrador se nao for x0
                    if (rd != 0)
                        x[rd] = (uint32_t)data;
                }
            }
            // lw funct3 == 010
            else if (funct3 == 0b010)
            {
                bool eh_ram = (address >= offset_ram && address <= max_ram);
                bool eh_clint = (address >= offset_clint && address <= max_clint);
                if (!eh_ram && !eh_clint)
                {
                    print_exception(pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address, log);
                    continue;
                }

                else
                {
                    const uint32_t data = load(mem, address, mtime, mtimecmp);
                    if (rd != 0) x[rd] = data;
                    sprintf(col1_addr, "0x%08x:lw", pc);
                    sprintf(col2_inst, "%s,%d(%s)", x_label[rd], (int32_t)simm, x_label[rs1]);
                    sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

                }
            }
            break;
        }
        case 0b1110011:
        {
            // Pega o registrador CSR
            uint32_t csr_antigo = csr_r(csr_address, &csr);
            // Constroi o operando
            uint32_t operando;
            if (funct3 & 0b100) // imediato
                operando = (uint32_t)rs1;
            else
                operando = x[rs1];

            // EBREAK
            if (funct3 == 0b000 && imm == 1)
            {
                fprintf(output, "0x%08x:ebreak\n", pc);
                // Retrieving previous and next instructions
                const uint32_t previous = ((uint32_t *)(mem))[(pc - 4 - offset) >> 2];
                const uint32_t next = ((uint32_t *)(mem))[(pc + 4 - offset) >> 2];
                // Halting condition
                if (previous == 0x01f01013 && next == 0x40705013)
                    run = 0;
            }
            // ECALL
            else if (imm == 0)
            {
                fprintf(output, "0x%08x:ecall\n", pc);
                print_exception(pc, 0, 11, "environment_call", output);
                exception_handler(&csr, &pc, 11, 0, log);
                continue;
            }
            // mret
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
                sprintf(col1_addr, "0x%08x:mret", pc);
                sprintf(col2_inst, "");
                sprintf(col3_details, "pc=0x%08x", csr_r(0x341, &csr));
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst);
                pc = csr_r(0x341, &csr) - 4;
            }
            // Inicializa new_data com o valor antigo por segurança (para casos de Read-Only)
            uint32_t new_data = csr_antigo;

            // --- CSRRW / CSRRWI (Atomic Swap) ---
            if ((funct3 & 0b011) == 0b001)
            {
                new_data = operando;
                csr_w(&csr, csr_address, new_data);

                if (funct3 == 0b001)
                { // CSRRW
                    sprintf(col1_addr, "0x%08x:csrrw", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address), x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s=%s=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), x_label[rs1], new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
                else
                { // CSRRWI
                    sprintf(col1_addr, "0x%08x:csrrwi", pc);
                    sprintf(col2_inst, "%s,%s,0x%02x", x_label[rd], csr_get_name(csr_address), operando);
                    sprintf(col3_details, "%s=%s=0x%08x,%s=0x%02x=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), operando, new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
            }

            // --- CSRRS / CSRRSI (Bit Set) ---
            else if ((funct3 & 0b011) == 0b010)
            {
                if (rs1 != 0)
                {
                    new_data = csr_antigo | operando;
                    csr_w(&csr, csr_address, new_data);
                }

                if (funct3 == 0b010)
                { // CSRRS
                    sprintf(col1_addr, "0x%08x:csrrs", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address), x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s|=%s=0x%08x|0x%08x=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), x_label[rs1], csr_antigo, operando, new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
                else
                { // CSRRSI
                    sprintf(col1_addr, "0x%08x:csrrsi", pc);
                    sprintf(col2_inst, "%s,%s,0x%02x", x_label[rd], csr_get_name(csr_address), operando);
                    // CORREÇÃO AQUI: Trocado 'csr_address' por 'csr_antigo'
                    sprintf(col3_details, "%s=%s=0x%08x,%s=0x%02x=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), operando, new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
            }

            // --- CSRRC / CSRRCI (Bit Clear) ---
            else if ((funct3 & 0b011) == 0b011)
            {

                if (rs1 != 0)
                {
                    new_data = csr_antigo & (~operando);
                    csr_w(&csr, csr_address, new_data);
                }

                if (funct3 == 0b011)
                { // CSRRC
                    sprintf(col1_addr, "0x%08x:csrrc", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], csr_get_name(csr_address), x_label[rs1]);
                    sprintf(col3_details, "%s=%s=0x%08x,%s&~=%s=0x%08x&~0x%08x=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), x_label[rs1], csr_antigo, operando, new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
                else
                { // CSRRCI
                    sprintf(col1_addr, "0x%08x:csrrci", pc);
                    sprintf(col2_inst, "%s,%s,0x%02x", x_label[rd], csr_get_name(csr_address), operando);
                    sprintf(col3_details, "%s=%s=0x%08x,%s&~=0x%02x=0x%08x&~0x%08x=0x%08x", x_label[rd], csr_get_name(csr_address), csr_antigo, csr_get_name(csr_address), operando, csr_antigo, operando, new_data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                }
            }

            if (funct3 != 0 && rd != 0)
                x[rd] = csr_antigo;

            break;
        }
        // tipo S
        case 0b0100011:
        {
            // sb funct3 == 000
            if (funct3 == 0b000)
            {
                const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                const uint32_t address = x[rs1] + simm;
                const uint32_t data_change = x[rs2] & 0xFF;

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:sb", pc);
                sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rs2], (uint16_t)(simm & 0xFFF), x_label[rs1]);
                sprintf(col3_details, "mem[0x%08x]=0x%02x", address, data_change);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

                *((uint8_t *)(mem + address - offset)) = (uint8_t)data_change;
            }
            // sw funct3 == 010
            else if (funct3 == 0b010)
            {
                const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                const uint32_t address = x[rs1] + simm;
                const uint32_t data_change = x[rs2];
                if (address + 4 > max_ram || address < offset_ram)
                {
                    // code 7: Store access fault
                    print_exception(pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address, log);
                    continue;
                }
                else if (address % 4 != 0)
                {
                    // code 6: Store address misaligned
                    print_exception(pc, address, 6, "store_fault", output);
                    exception_handler(&csr, &pc, 6, address, log);
                    continue;
                }
                else
                {
                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:sw", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rs2], (uint16_t)(simm & 0xFFF), x_label[rs1]);
                    sprintf(col3_details, "mem[0x%08x]=0x%08x", address, data_change);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

                    *((uint32_t *)(mem + address - offset)) = data_change;
                }
            }
            // sh funct3 == 001
            else if (funct3 == 0b001)
            {
                const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                const uint32_t address = x[rs1] + simm;
                const uint32_t data_change = x[rs2] & 0xFFFF;
                if (address % 2 != 0)
                {
                    // code 6: Store address misaligned
                    print_exception(pc, address, 6, "store_fault", output);
                    exception_handler(&csr, &pc, 6, address, log);
                    continue;
                }
                else if (address + 2 > max_ram || address < offset_ram)
                {
                    // code 7: Store access fault
                    print_exception(pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address, log);
                    continue;
                }
                else
                {
                    // imprimindo instrucao no arquivo
                    sprintf(col1_addr, "0x%08x:sh", pc);
                    sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rs2], (uint16_t)(simm & 0xFFF), x_label[rs1]);
                    sprintf(col3_details, "mem[0x%08x]=0x%04x", address, data_change);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

                    *((uint16_t *)(mem + address - offset)) = (uint16_t)data_change;
                }
            }
            break;
        }
        // tipo B
        case 0b1100011:
        {
            const int32_t simm = (b_imm2 & 0x1000) ? (b_imm2 | 0xFFFFE000) : b_imm2;
            // beq
            if (funct3 == 0b000)
            {
                // calculando endereco da operacao
                const uint32_t address = pc + simm;
                // verificando condicao
                int condition = (int32_t)x[rs1] == (int32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:beq", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x==0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // definindo proximo pc
                if (condition)
                    pc = address - 4;
            }
            // bne
            else if (funct3 == 0b001)
            {
                const uint32_t address = pc + simm;
                // verificando condicao
                int condition = (int32_t)x[rs1] != (int32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:bne", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x!=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // definindo proximo pc
                if (condition)
                    pc = address - 4;
            }
            // blt
            else if (funct3 == 0b100)
            {
                uint32_t address = pc + simm;

                int condition = (int32_t)x[rs1] < (int32_t)x[rs2];

                sprintf(col1_addr, "0x%08x:blt", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

                if (condition)
                    pc = address - 4;
            }
            // bge
            else if (funct3 == 0b101)
            {
                const uint32_t address = pc + simm;
                // verificando condicao
                int condition = (int32_t)x[rs1] >= (int32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:bge", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // definindo proximo pc
                if (condition)
                    pc = address - 4;
            }
            // bltu
            else if (funct3 == 0b110)
            {
                const uint32_t address = pc + simm;
                // verificando condicao
                int condition = (uint32_t)x[rs1] < (uint32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:bltu", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x<0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // definindo proximo pc
                if (condition)
                    pc = address - 4;
            }
            // bgeu
            else if (funct3 == 0b111)
            {
                const uint32_t address = pc + simm;
                // verificando condicao
                int condition = (uint32_t)x[rs1] >= (uint32_t)x[rs2];

                // imprimindo instrucao no arquivo
                sprintf(col1_addr, "0x%08x:bgeu", pc);
                sprintf(col2_inst, "%s,%s,0x%03x", x_label[rs1], x_label[rs2], b_imm2 >> 1);
                sprintf(col3_details, "(0x%08x>=0x%08x)=%d->pc=0x%08x", x[rs1], x[rs2], condition, condition ? address : pc + 4);
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                // definindo proximo pc
                if (condition)
                    pc = address - 4;
            }
            break;
        }
        // tipo U
        case 0b0110111:
        {
            // lui
            uint32_t imm20_a = instruction >> 12;
            const uint32_t data = imm20_a << 12;

            // imprimindo instrucao no arquivo
            sprintf(col1_addr, "0x%08x:lui", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_a & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x", x_label[rd], data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            // atualizando registrador se nao for x0
            if (rd != 0)
                x[rd] = data;
            break;
        }
        // tipo U
        case 0b0010111:
        {
            // auipc

            const int32_t imm_u_val = (int32_t)(instruction & 0xFFFFF000);
            // Some o valor (que já está 'deslocado') ao PC
            const uint32_t data = pc + imm_u_val;

            // Para o log, podemos mostrar os 20 bits originais
            const uint32_t imm20_for_log = (uint32_t)imm_u_val >> 12;

            sprintf(col1_addr, "0x%08x:auipc", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_for_log & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], pc, imm_u_val, data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);

            if (rd != 0)
                x[rd] = data;
            break;
        }
        // tipo J
        case 0b1101111:
        {
            // imm[20] (bit 31 da instrução) -> movido para bit 20
            const uint32_t imm20_bit = (instruction & 0x80000000) >> 11;
            // imm[10:1] (bits 30-21 da instrução) -> movido para bits 10-1
            const uint32_t imm10_1_bits = (instruction & 0x7FE00000) >> 20;
            // imm[11] (bit 20 da instrução) -> movido para bit 11
            const uint32_t imm11_bit = (instruction & 0x00100000) >> 9;
            // imm[19:12] (bits 19-12 da instrução) -> já estão nos bits 19-12
            const uint32_t imm19_12_bits = (instruction & 0x000FF000);

            // Juntando os pedaços
            const uint32_t imm_j = imm20_bit | imm10_1_bits | imm11_bit | imm19_12_bits;
            const int32_t simm = (imm_j & 0x00100000) ? (imm_j | 0xFFE00000) : imm_j;
            const uint32_t address = pc + simm;

            sprintf(col1_addr, "0x%08x:jal", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], (simm >> 1) & 0xFFFFF);
            sprintf(col3_details, "pc=0x%08x,%s=0x%08x", address, x_label[rd], pc + 4);
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
            sprintf(col1_addr, ">exception:illegal_instruction");
            sprintf(col2_inst, "");
            sprintf(col3_details, "cause=0x%08x,epc=0x%08x,tval=0x%08x", mcause, mepc, mtval);
            fprintf(output, "%-15s%-8s%s\n", col1_addr, col2_inst, col3_details);
            continue;
        }
        }
        pc += 4;
    }

    free(mem);
    fclose(output);
    fclose(input);
    fclose(log);
    printf("--------------------------------------------------------------------------------\n");
    return 0;
}