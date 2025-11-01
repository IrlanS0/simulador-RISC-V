#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("--------------------------------------------------------------------------------\n");
    FILE *input = fopen(argv[1], "r");
    if (!input) {
        perror("Erro ao abrir arquivo");
        return 1;
    };
    FILE *output = fopen(argv[2], "w");
    if (!output) {
        perror("Erro ao abrir arquivo");
        fclose(input);
        return 1;
    };

    char token[32];  // Para armazenar cada “palavra” lida (ex: "@80000000", "6F", "00")
    const uint32_t offset = 0x80000000;
    size_t i = 0;
    //declarando registradores
    uint32_t x[32] = { 0 };
    const char* x_label[32] = { "zero", "ra", "sp", "gp", 
        "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", 
        "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", 
        "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };
    uint32_t pc = offset;
    uint8_t* mem = (uint8_t*)malloc(32 * 1024); // 32 KiB de memória
    if(!mem) {
        perror("Erro ao alocar memória");
        fclose(input);
        fclose(output);
        return 1;
    }
    
    while (fscanf(input, "%31s", token) == 1) {
        // Se começa com '@', é um endereço -> apenas ignore
        if (token[0] == '@') {
            continue;
        }

        // Caso contrário, converte o texto hexadecimal em número
        unsigned int valor;
        if (sscanf(token, "%x", &valor) == 1) {
            if (i < 32 * 1024){
                mem[i++] = (uint8_t)valor;
            }
        }
    }

    uint8_t run = 1;
    
    while(run){
        if(pc < offset){
            printf("error: pc out of bounds at pc = 0x%08x\n", pc);
            break;
        }
        // alinhando os 4 bytes da memoria
        const uint32_t instruction = ((uint32_t*)(mem))[(pc - offset) >> 2];
        // decodificando os opcodes
        const uint8_t opcode = instruction & 0b1111111;
        // recuperando campos da instrucao
        const uint8_t funct7 = instruction >> 25;
        const uint16_t imm = instruction >> 20;
        const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
        const uint16_t b_imm = ((instruction >> 31) & 0b1) << 11 | 
        ((instruction >> 7) & 0b1) << 10 |
        ((instruction >> 25) & 0b111111) << 4 |
        ((instruction >> 8) & 0b1111);
        const uint16_t s_imm = ((instruction >> 25) & 0b1111111) << 5 |
        ((instruction >> 7) & 0b11111);
        const uint8_t rs1 = (instruction & (0b11111 << 15)) >> 15;
        const uint8_t rs2 = (instruction & (0b11111 << 20)) >> 20;
        const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
        const uint8_t rd = (instruction & (0b11111 << 7)) >> 7;
        const uint32_t imm20 = ((instruction >> 31) << 19) |
        (((instruction & (0b11111111 << 12)) >> 12) << 11) |
        (((instruction & (0b1 << 20)) >> 20) << 10) |
        ((instruction & (0b1111111111 << 21)) >> 21);
        // fprintf(output, "PC: 0x%08x Instruction: 0x%08x Opcode: 0x%08x\nfunct7: 0x%08x\nimm: 0x%08x\nuimm: 0x%08x\nimm20: 0x%08x\nb_imm: 0x%08x\ns_imm: 0x%08x\nrs2: 0x%08x\nrs1: 0x%08x\nfunct3: 0x%08x\nrd: 0x%08x\n", pc, instruction, opcode, funct7, imm, uimm, rs1, funct3, rd);
        
        switch(opcode){
            //tipo R
            case 0b0110011:{
                //add funct3 == 000 and funct7 == 0000000
                if(funct3 == 0b000 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] + x[rs2];
                    // imprimindo instrucao no arquivo
                    char col1_addr[20];
                    char col2_inst[30];
                    char col3_details[60];

                    sprintf(col1_addr, "0x%08x:add", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // fprintf(output, "0x%08x:add    %s,%s,%s     %s=0x%08x+0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                //sub funct3 == 000 and funct7 == 0100000
                else if(funct3 == 0b000 && funct7 == 0b0100000){
                    const int32_t data = (int32_t)x[rs1] - (int32_t)x[rs2]; 
                    char col1_addr[20];
                    char col2_inst[30];
                    char col3_details[60];

                    sprintf(col1_addr, "0x%08x:sub", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x-0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                    // imprimindo instrucao no arquivo
                    // fprintf(output, "0x%08x:sub    %s,%s,%s     %s=0x%08x-0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], (uint32_t)data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = (uint32_t)data;
                }
                // sll funct3 == 001 and funct7 == 0000000
                else if(funct3 == 0b001 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] << x[rs2];
                    char col1_addr[20];
                    char col2_inst[30];
                    char col3_details[60];

                    sprintf(col1_addr, "0x%08x:sll", pc);
                    sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
                    sprintf(col3_details, "%s=0x%08x<<0x%08x=0x%08x", x_label[rd], x[rs1], x[rs2], data);
                    // imprimindo instrucao no arquivo
                    // fprintf(output, "0x%08x:sll    %s,%s,%s     %s=0x%08x<<0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // srl funct3 == 101 and funct7 == 0000000
                else if(funct3 == 0b101 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] >> x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:srl    %s,%s,%s     %s=0x%08x>>0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // sra funct3 == 101 and funct7 == 0100000
                else if(funct3 == 0b101 && funct7 == 0b0100000){
                    const uint32_t data = (int32_t)x[rs1] >> x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sra    %s,%s,%s     %s=0x%08x>>0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // slt funct3 == 010 and funct7 == 0000000
                else if(funct3 == 0b010 && funct7 == 0b0000000){
                    const uint32_t data = ((int32_t)x[rs1] < (int32_t)imm) ? 1 : 0;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:slt    %s,%s,%s     %s=(0x%08x<0x%08x)=u1\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // sltu funct3 == 011 and funct7 == 0000000
                else if(funct3 == 0b011 && funct7 == 0b0000000){
                    const uint32_t data = (x[rs1] < x[rs2]) ? 1 : 0;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sltu   %s,%s,%s     %s=(0x%08x<0x%08x)=u1\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2]);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // or fucnt3 == 110 and funct7 == 0000000
                else if(funct3 == 0b110 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] | x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:or     %s,%s,%s     %s=0x%08x|0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // and funct3 == 111 and funct7 == 0000000
                else if(funct3 == 0b111 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] & x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:and    %s,%s,%s     %s=0x%08x&0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // xor funct3 == 100 and funct7 == 0000000
                else if(funct3 == 0b100 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] ^ x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:xor    %s,%s,%s     %s=0x%08x^0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // mul funct3 == 000 and funct7 == 0000001
                else if(funct3 == 0b000 && funct7 == 0b0000001){
                    const int64_t data = (int32_t)x[rs1] * (int32_t)x[rs2];
                    const int32_t data_low = (int32_t)(data);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:mul    %s,%s,%s     %s=0x%08x*0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data_low);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data_low;
                }
                // mulh funct3 == 001 and funct7 == 0000001
                else if(funct3 == 0b001 && funct7 == 0b0000001){
                    const int64_t data = (int32_t)x[rs1] * (int32_t)x[rs2];
                    const int32_t data_high = (int32_t)(data >> 32);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:mulh   %s,%s,%s     %s=0x%08x*0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data_high);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data_high;
                }
                // mulhsu funct3 == 010 and funct7 == 0000001
                else if(funct3 == 0b010 && funct7 == 0b0000001){
                    const int64_t data = (int32_t)x[rs1] * (uint32_t)x[rs2];
                    const int32_t data_high = (int32_t)(data >> 32);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:mulhsu %s,%s,%s     %s=0x%08x*0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data_high);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data_high;
                }
                // mulhu funct3 == 011 and funct7 == 0000001
                else if(funct3 == 0b011 && funct7 == 0b0000001){
                    const uint64_t data = (uint32_t)x[rs1] * (uint32_t)x[rs2];
                    const uint32_t data_high = (uint32_t)(data >> 32);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:mulhu  %s,%s,%s     %s=0x%08x*0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data_high);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data_high;
                }
                // div funct3 == 100 and funct7 == 0000001
                else if(funct3 == 0b100 && funct7 == 0b0000001){
                    const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : (uint32_t)((int32_t)x[rs1] / (int32_t)x[rs2]);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:div    %s,%s,%s     %s=0x%08x/0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // divu funct3 == 101 and funct7 == 0000001
                else if(funct3 == 0b101 && funct7 == 0b0000001){
                    const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : x[rs1] / x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:divu   %s,%s,%s     %s=0x%08x/0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // rem funct3 == 110 and funct7 == 0000001
                else if(funct3 == 0b110 && funct7 == 0b0000001){
                    const uint32_t data = (x[rs2] == 0) ? x[rs1] : (uint32_t)((int32_t)x[rs1] % (int32_t)x[rs2]);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:rem    %s,%s,%s     %s=0x%08x%%0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // remu funct3 == 111 and funct7 == 0000001
                else if(funct3 == 0b111 && funct7 == 0b0000001){
                    const uint32_t data = (x[rs2] == 0) ? x[rs1] : x[rs1] % x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:remu   %s,%s,%s     %s=0x%08x%%0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1], x[rs2], data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                break;
            }
            //tipo I
            case 0b0010011:{
                // slli (funct3 == 0b001 && funct7 == 0b0000000)
                if(funct3 == 0b001){
                    const uint32_t data = x[rs1] << uimm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:slli   %s,%s,%u      %s=0x%08x<<%u=0x%08x\n", pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // srli funct3 == 101 and funct7 == 0000000
                else if(funct3 == 0b101 && funct7 == 0b0000000){
                    const uint32_t data = x[rs1] >> uimm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:srli   %s,%s,%u      %s=0x%08x>>%u=0x%08x\n", pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                //srai funct3 == 101 and funct7 == 0100000
                else if(funct3 == 0b101 && funct7 == 0b0100000){
                    const uint32_t data = (int32_t)x[rs1] >> (int32_t)uimm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:srai   %s,%s,%u      %s=0x%08x>>%u=0x%08x\n", pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // slti funct3 == 010 and funct7 == 0000000
                else if(funct3 == 0b010){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t data = ((int32_t)x[rs1] < (int32_t)simm) ? 1 : 0;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:slti   %s,%s,0x%03x   %s=(0x%08x<0x%08x)=u1\n", pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // sltiu funct3 == 011 and funct7 == 0000000
                else if(funct3 == 0b011){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t data = (x[rs1] < simm) ? 1 : 0;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sltiu  %s,%s,0x%03x   %s=(0x%08x<0x%08x)=u1\n", pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // xori funct3 == 100 and funct7 == 0000000
                else if(funct3 == 0b100){
                    const uint32_t data = x[rs1] ^ imm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:xori   %s,%s,0x%03x   %s=0x%08x^0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // andi funct3 == 111 and funct7 == 0000000
                else if(funct3 == 0b111){
                    const uint32_t data = x[rs1] & imm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:andi   %s,%s,0x%03x   %s=0x%08x&0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // ori funct3 == 110 and funct7 == 0000000
                else if(funct3 == 0b110){
                    const uint32_t data = x[rs1] | imm;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:ori    %s,%s,0x%03x   %s=0x%08x|0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                // addi funct3 == 000 and funct7 == 0000000
                else if(funct3 == 0b000){
                    uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t data = x[rs1] + (int32_t)simm;
                    char col1_addr[20];
                    char col2_inst[30];
                    char col3_details[60];

                    sprintf(col1_addr, "0x%08x:addi", pc);
                    sprintf(col2_inst, "%s,%s,0x%03x", x_label[rd], x_label[rs1], imm);
                    sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1], simm, data);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                    // fprintf(output, "0x%08x:addi   %s,%s,0x%03x   %s=0x%08x+0x%08x=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], simm, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                break;
            }
            case 0b1100111:{
                // jalr tipo I
                if(funct3 == 0b000){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = (x[rs1] + simm) & ~1u; //o padrao risc-v zera o bit 0
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:jalr   %s,%s,0x%03x   pc=0x%08x+0x%08x,%s=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, address, simm, x_label[rd], pc + 4);
                    // atualiza registrador se nao for x0
                    if(rd != 0) x[rd] = pc + 4;
                    pc = address - 4; // subtraindo 4 pois o pc sera incrementado apos o switch
                }
                break;
            }
            // tipo I
            case 0b0000011:{
                // lb funct3 == 000
                if(funct3 == 0b000){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = x[rs1] + simm;
                    // Leia 1 byte da memória no endereço efetivo (address), e interprete esse byte como signed (int8_t).
                    const int8_t data = *((int8_t*)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:lb     %s,0x%03x(%s)  %s=mem[0x%08x]=0x%08x\n", pc, x_label[rd], simm, x_label[rs1], x_label[rd], address, (uint8_t)data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = (int32_t)data;
                }
                // lbu funct3 == 100
                else if(funct3 == 0b100){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = x[rs1] + simm;
                    // Leia 1 byte da memória no endereço efetivo (address), e interprete esse byte como unsigned (uint8_t).
                    const uint8_t data = *((uint8_t*)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:lbu    %s,0x%03x(%s)  %s=mem[0x%08x]=0x%08x\n", pc, x_label[rd], simm, x_label[rs1], x_label[rd], address, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = (uint8_t)data;
                }
                // lh funct3 == 001
                else if(funct3 == 0b001){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = x[rs1] + simm;
                    // lendo 2 bytes da memoria
                    const int16_t data = *((uint16_t*)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:lh     %s,0x%03x(%s)  %s=mem[0x%08x]=0x%08x\n", pc, x_label[rd], simm, x_label[rs1], x_label[rd], address, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = (int32_t)data;
                }
                // lhu funct3 == 101
                else if(funct3 == 0b101){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = x[rs1] + simm;
                    // lendo 2 bytes da memoria
                    const uint16_t data = *((uint16_t*)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:lhu    %s,0x%03x(%s)  %s=mem[0x%08x]=0x%08x\n", pc, x_label[rd], simm, x_label[rs1], x_label[rd], address, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = (uint32_t)data;
                }
                // lw funct3 == 010
                else if(funct3 == 0b010){
                    const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
                    const uint32_t address = x[rs1] + simm;
                    // lendo 4 bytes da memoria
                    const uint32_t data = *((uint32_t*)(mem + address - offset));
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:lw     %s,0x%03x(%s)  %s=mem[0x%08x]=0x%08x\n", pc, x_label[rd], simm, x_label[rs1], x_label[rd], address, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                break;
            }
            // ebreak
            case 0b1110011:{
                // ebreak (funct3 == 000 and imm == 1)
                if(funct3 == 0b000 && imm == 1) {
                        // Outputting instruction to console
                    fprintf(output,"0x%08x:ebreak\n", pc);
                    // Retrieving previous and next instructions
                    const uint32_t previous = ((uint32_t*)(mem))[(pc - 4 - offset) >> 2];
                    const uint32_t next = ((uint32_t*)(mem))[(pc + 4 - offset) >> 2];
                    // Halting condition
                    if(previous == 0x01f01013 && next == 0x40705013) run = 0;
                }
                    // Breaking case
            break;
                }
            // tipo S
            case 0b0100011: {
                // sb funct3 == 000
                if(funct3 == 0b000){
                    const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                    const uint32_t address = x[rs1] + simm;
                    const uint32_t data_change = x[rs2] & 0xFF;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sb     %s,0x%03x(%s) mem[0x%08x]=0x%02x\n", pc, x_label[rs2], simm, x_label[rs1], address, data_change);
                    
                    *((uint8_t*)(mem + address - offset)) = (uint8_t)data_change;
                }
                // sw funct3 == 010
                else if(funct3 == 0b010){
                    const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                    const uint32_t address = x[rs1] + simm;
                    const uint32_t data_change = x[rs2];
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sw     %s,0x%03x(%s) mem[0x%08x]=0x%08x\n", pc, x_label[rs2], simm, x_label[rs1], address, data_change);

                    *((uint32_t*)(mem + address - offset)) = data_change;
                }
                // sh funct3 == 001
                else if(funct3 == 0b001){
                    const uint32_t simm = (s_imm >> 11) ? (0xFFFFF000 | s_imm) : (s_imm);
                    const uint32_t address = x[rs1] + simm;
                    const uint32_t data_change = x[rs2] & 0xFFFF;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:sh     %s,0x%03x(%s) mem[0x%08x]=0x%04x\n", pc, x_label[rs2], simm, x_label[rs1], address, data_change);
                    
                    *((uint16_t*)(mem + address - offset)) = (uint16_t)data_change;
                }
                break;
            }
            // tipo B
            case 0b1100011:{
                // beq
                if(funct3 == 0b000){
                    // executando extensao de sinal no campo imediato
                    const uint32_t simm = (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : (b_imm);
                    //calculando endereco da operacao
                    const uint32_t address = pc + simm;
                    // verificando condicao
                    int condition = (int32_t)x[rs1] == (int32_t)x[rs2];
                        // imprimindo instrucao no arquivo
                        fprintf(output, "0x%08x:beq    %s,%s,0x%03x  (0x%08x==0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], simm, x[rs1], x[rs2], condition, condition ? address : pc);
                        // definindo proximo pc
                        if(condition)pc = address;
                }
                //bne
                else if(funct3 == 0b001){
                    const uint32_t simm = (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : (b_imm);
                    const uint32_t address = pc + simm;
                    // verificando condicao
                    int condition = (int32_t)x[rs1] != (int32_t)x[rs2];
                        // imprimindo instrucao no arquivo
                        fprintf(output, "0x%08x:bne    %s,%s,0x%03x  (0x%08x!=0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], simm, x[rs1], x[rs2], condition, condition ? address : pc + 4);
                        // definindo proximo pc
                        if(condition)pc = address;
                }
                // blt 
                else if(funct3 == 0b100){
                    uint32_t address = pc + (uint32_t)b_imm;

                    int condition = (int32_t)x[rs1] < (int32_t)x[rs2];
                    fprintf(output,"0x%08x:blt    %s,%s,0x%03x  (0x%08x<0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], b_imm, x[rs1], x[rs2], condition, condition ? address : pc + 4);

                    if(condition)pc = address;
                }
                //bge
                else if(funct3 == 0b101){
                    const uint32_t simm = (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : (b_imm);
                    const uint32_t address = pc + simm;
                    // verificando condicao
                    int condition = (int32_t)x[rs1] >= (int32_t)x[rs2];
                        // imprimindo instrucao no arquivo
                        fprintf(output, "0x%08x:bge    %s,%s,0x%03x  (0x%08x>=0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], simm, x[rs1], x[rs2], condition, condition ? address : pc + 4);
                        // definindo proximo pc
                        if(condition)pc = address;
                }
                //bltu
                else if(funct3 == 0b110){
                    const uint32_t simm = (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : (b_imm);
                    const uint32_t address = pc + simm;
                    //verificando condicao
                    int condition = (uint32_t)x[rs1] < (uint32_t)x[rs2];
                        // imprimindo instrucao no arquivo
                        fprintf(output, "0x%08x:bltu   %s,%s,0x%03x  (0x%08x<0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], simm, x[rs1], x[rs2], condition, condition ? address : pc + 4);
                        // definindo proximo pc
                        if(condition)pc = address;
                }
                //bgeu
                else if(funct3 == 0b111){
                    const uint32_t simm = (b_imm & 0x1000) ? (0xFFFFE000 | b_imm) : (b_imm);
                    const uint32_t address = pc + simm;
                    //verificando condicao
                    int condition = (uint32_t)x[rs1] >= (uint32_t)x[rs2];
                        // imprimindo instrucao no arquivo
                        fprintf(output, "0x%08x:bgeu   %s,%s,0x%03x  (0x%08x>=0x%08x)=%d->pc=0x%08x\n", pc, x_label[rs1], x_label[rs2], simm, x[rs1], x[rs2], condition, condition ? address : pc + 4);
                        // definindo proximo pc
                        if(condition)pc = address;
                }
                break;
            }
            // tipo U
            case 0b0110111:{
                // lui 
                int32_t imm20_a = (int32_t)instruction >> 12;
                const uint32_t data = imm20_a << 12;
                // imprimindo instrucao no arquivo
                fprintf(output, "0x%08x:lui    %s,0x%05x     %s=0x%08x\n", pc, x_label[rd], imm20_a, x_label[rd], data);
                // atualizando registrador se nao for x0
                if(rd != 0) x[rd] = data;
                break;
            }
            // tipo U
            case 0b0010111:{
                // auipc 
                int32_t imm20_a = (int32_t)instruction >> 12;
                if(imm20_a != 0){
                    const uint32_t data = pc + (imm20_a << 12);
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:auipc  %s,0x%05x  %s=0x%08x+0x%08x=0x%08x\n", pc, x_label[rd], imm20_a, x_label[rd], pc, (imm20_a << 12), data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }else{
                    const uint32_t data = pc;
                    // imprimindo instrucao no arquivo
                    fprintf(output, "0x%08x:auipc  %s,0x%05x     %s=0x%08x+0x%08x=0x%08x\n", pc, x_label[rd], imm20_a, x_label[rd], (imm20_a << 12), pc, data);
                    // atualizando registrador se nao for x0
                    if(rd != 0) x[rd] = data;
                }
                break;
            }
            // tipo J
            case 0b1101111:{
                // jal funct3 == 000 and funct7 == 0000000
                // executando extensao de sinal no campo imediato
                const uint32_t simm = (imm20 >> 19) ? (0xFFF00000 | imm20) : (imm20);
                //calculando endereco da operacao
                const uint32_t address = pc + (simm << 1);
                // imprimindo instrucao no arquivo
                fprintf(output, "0x%08x:jal    %s,0x%05x     pc=0x%08x,%s=0x%08x\n", pc, x_label[rd], imm20, address, x_label[rd], pc + 4);
                
                // atualiza registrador se nao for x0
                if(rd != 0) x[rd] = pc + 4;
                // definindo proximo pc menos 4
                pc = address - 4;
                break;
            }
                default:  
                printf("error: unknown instruction opcode at pc = 0x%08x\n", pc);
				// Halting simulation
			run = 0;
        }
        pc += 4;
    }
    
    free(mem);
    fclose(output);
    fclose(input);
    printf("--------------------------------------------------------------------------------\n");
    return 0;
}