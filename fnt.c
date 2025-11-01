// Standard integer library
#include <stdint.h>
// Standard library
#include <stdlib.h>
// Standard I/O library
#include <stdio.h>

/**
 * Main function
 * @param argc	Number of command line arguments
 * @param argv	Command line arguments
 * @return		Returns the program execution status
 */
int main(int argc, char* argv[]) {
	// Outputting separator
	printf("--------------------------------------------------------------------------------\n");
	// Iterating over arguments
	// for(uint32_t i = 0; i < argc; i++) {
	// 	// Outputting argument
	// 	printf("argv[%i] = %s\n", i, argv[i]);
	// }
	// Opening input and output files using proper permissions
	// FILE* input = fopen(argv[1], "r");
	// FILE* output = fopen(argv[2], "w");
	// FILE* terminal_in = fopen(argv[3], "r");  // OPTIONAL
	// FILE* terminal_out = fopen(argv[4], "w"); // OPTIONAL
	// Setting memory offset to 0x80000000
	const uint32_t offset = 0x80000000;
	// Creating 32 registers initialized with zero and labels
	uint32_t x[32] = { 0 };
	const char* x_label[32] = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };
	// Creating pc register initialized with memory offset
	uint32_t pc = offset;
	// Creating 32 KiB memory for both data and instructions
	uint8_t* mem = (uint8_t*)(malloc(32 * 1024));
	// Reading memory contents from input hexadecimal file
	// .
	// .
	// .
	// Manually filling memory just for testing purposes
	// 80000000: 100000ef jal    0x80000100
	// ...
	// 80000100: 01f01013 slli   zero,zero,0x1f
	// 80000104: 00100073 ebreak
	// 80000108: 40705013 srai   zero,zero,0x7
	mem[0x80000000 - offset] = 0xef;
	mem[0x80000001 - offset] = 0x00;
	mem[0x80000002 - offset] = 0x00;
	mem[0x80000003 - offset] = 0x10;
	mem[0x80000100 - offset] = 0x13;
	mem[0x80000101 - offset] = 0x10;
	mem[0x80000102 - offset] = 0xf0;
	mem[0x80000103 - offset] = 0x01;
	mem[0x80000104 - offset] = 0x73;
	mem[0x80000105 - offset] = 0x00;
	mem[0x80000106 - offset] = 0x10;
	mem[0x80000107 - offset] = 0x00;
	mem[0x80000108 - offset] = 0x13;
	mem[0x80000109 - offset] = 0x50;
	mem[0x8000010a - offset] = 0x70;
	mem[0x8000010b - offset] = 0x40;
	// Outputting separator
	printf("--------------------------------------------------------------------------------\n");
	// Setting run condition
	uint8_t run = 1;
	// Loop while condition is true
	while(run) {
			// mem[0x80000000 - offset] = 0xef;
		// Reading instruction from memory (4 byte alignment)
		const uint32_t instruction = ((uint32_t*)(mem))[(pc - offset) >> 2];
		// Retrieving instruction opcode (6:0)
		const uint8_t opcode = instruction & 0b1111111;
		// Retrieving instruction fields
		const uint8_t funct7 = instruction >> 25;
		const uint16_t imm = instruction >> 20;
		const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
		const uint8_t rs1 = (instruction & (0b11111 << 15)) >> 15;
		const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
		const uint8_t rd = (instruction & (0b11111 << 7)) >> 7;
		const uint32_t imm20 = ((instruction >> 31) << 19) | (((instruction & (0b11111111 << 12)) >> 12) << 11) | (((instruction & (0b1 << 20)) >> 20) << 10) | ((instruction & (0b1111111111 << 21)) >> 21);
		// Checking instruction opcode
		switch(opcode) {
			// I type (0010011)
			case 0b0010011:
			{
				// slli (funct3 == 001 and funct7 == 0000000)
				if(funct3 == 0b001 && funct7 == 0b0000000) {
					// Calculating operation data
					const uint32_t data = x[rs1] << uimm;
					// Outputting instruction to console
					printf("0x%08x:slli   %s,%s,%u  %s=0x%08x<<%u=0x%08x\n", pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
					// Updating register if not x[0] (zero)
					if(rd != 0) x[rd] = data;
				}
				// Breaking case
				break;
				// I type (1110011)
			}
			case 0b1110011:
				// ebreak (funct3 == 000 and imm == 1)
				if(funct3 == 0b000 && imm == 1) {
					// Outputting instruction to console
					printf("0x%08x:ebreak\n", pc);
					// Retrieving previous and next instructions
					const uint32_t previous = ((uint32_t*)(mem))[(pc - 4 - offset) >> 2];
					const uint32_t next = ((uint32_t*)(mem))[(pc + 4 - offset) >> 2];
					// Halting condition
            		if(previous == 0x01f01013 && next == 0x40705013) run = 0;
				}
				// Breaking case
				break;
			// J type (1101111)
			case 0b1101111:
			{
				// Performing sign extension in immediate field
				const uint32_t simm = (imm20 >> 19) ? (0xFFF00000 | imm20) : (imm20);
				// Calculating operation address
				const uint32_t address = pc + (simm << 1);
				// Outputting instruction to console
				printf("0x%08x:jal    %s,0x%05x    pc=0x%08x,%s=0x%08x\n", pc, x_label[rd], imm, address, x_label[rd], pc + 4);
				// Updating register if not x[0] (zero)
				if(rd != 0) x[rd] = pc + 4;
				// Setting next pc minus 4
				pc = address - 4;
				// Breaking case
				break;
			}
			// Unknown
			default:
				// Outputting error message
				printf("error: unknown instruction opcode at pc = 0x%08x\n", pc);
				// Halting simulation
				run = 0;
		}
		// Incrementing pc by 4
		pc = pc + 4;
	}
	// Closing input and output files
	// fclose(input);
	// fclose(output);
	// fclose(terminal_in);
	// fclose(terminal_out);
	// Outputting separator
	printf("--------------------------------------------------------------------------------\n");
	// Returning success status
	return 0;
}