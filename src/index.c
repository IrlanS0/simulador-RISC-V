#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
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
#define PLIC_SOURCE_COUNT 1024
#define PLIC_CONTEXT_COUNT 1
#define CACHE_SIZE 256
#define BLOCK_SIZE 16
#define ASSOCIATIVITY 2
#define NUM_SETS 8
#define MAX_AGE 255
static FILE *logi;

void init_log(void)
{
    logi = fopen("log.txt", "w");
}

typedef struct
{
    uint8_t valid;
    uint8_t dirty;
    uint32_t age;
    uint32_t id;
    uint8_t data[BLOCK_SIZE];
} CACHELINE;

typedef struct
{
    uint32_t size;
    uint32_t block_size;
    uint32_t associativity;
    uint32_t num_sets;

    float total_accesses;
    float hits;

    CACHELINE lines[NUM_SETS][ASSOCIATIVITY];
} CACHE;

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

typedef struct
{
    // Registradores UART
    uint8_t rhr;
    uint8_t ier;
    uint8_t isr;
    uint8_t lsr;
} UART_REG;

typedef struct
{
    uint32_t priority[PLIC_SOURCE_COUNT];
    uint32_t pending[PLIC_SOURCE_COUNT / 32];
    uint32_t enable[PLIC_CONTEXT_COUNT][PLIC_SOURCE_COUNT / 32];
    uint32_t threshold[PLIC_SOURCE_COUNT];
    uint32_t claim[PLIC_SOURCE_COUNT];
} PLIC_REG;

void init_cache(CACHE *cache)
{
    cache->total_accesses = 0;
    cache->hits = 0;

    cache->size = CACHE_SIZE;
    cache->block_size = BLOCK_SIZE;
    cache->associativity = ASSOCIATIVITY;
    cache->num_sets = cache->size / (cache->block_size * cache->associativity);
    for (uint32_t i = 0; i < cache->num_sets; i++)
    {
        for (uint32_t j = 0; j < cache->associativity; j++)
        {
            cache->lines[i][j].valid = 0;
            cache->lines[i][j].dirty = 0;
            cache->lines[i][j].age = 0;
            cache->lines[i][j].id = 0;
            memset(cache->lines[i][j].data, 0, cache->block_size);
        }
    }
};

void print_cache(CACHE *cache, char name_event[5], uint32_t pc, FILE *output, uint32_t evento, uint32_t cache_index, uint32_t i)
{
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];
    uint32_t age = cache->lines[cache_index][i].age;
    uint32_t id = cache->lines[cache_index][i].id;
    uint32_t vetor_valido[cache->associativity];
    uint32_t vetor_age[cache->associativity];
    uint32_t vetor_id[cache->associativity];

    sprintf(col1_addr, "#cache_mem:%s", name_event);
    sprintf(col2_inst, "0x%08x", pc);
    if (evento > 3 &&  evento <= 7)
    {
        uint32_t *words = (uint32_t *)cache->lines[cache_index][i].data;
        if (evento != 7)
            sprintf(col3_details, "line=%u,age=%u,id=0x%07x,block[%u]={0x%08x,0x%08x,0x%08x,0x%08x}", cache_index, age, id, i, words[0], words[1], words[2], words[3]);
        else 
            sprintf(col3_details, "line=%u,age=%u,id=0x%06x,block[%u]={0x%08x,0x%08x,0x%08x,0x%08x}", cache_index, age, id, i, words[0], words[1], words[2], words[3]);
        // fprintf(logi, "block[%u]={0x%08x, 0x%08x, 0x%08x, 0x%08x}\n", i, words[0], words[1], words[2], words[3]);
    }
    else if (evento == 1 || evento == 2 || evento == 3 || evento == 8)
    {
        for (uint32_t i = 0; i < cache->associativity; i++)
        {
            vetor_age[i] = cache->lines[cache_index][i].age;
            vetor_valido[i] = cache->lines[cache_index][i].valid;
            vetor_id[i] = cache->lines[cache_index][i].id;
        }
        sprintf(col3_details, "line=%u,valid={%u,%u},age={%u,%u},id={0x%06x,0x%06x}", cache_index, vetor_valido[0], vetor_valido[1], vetor_age[0], vetor_age[1], vetor_id[0], vetor_id[1]);
    }
    fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
    // #cache_mem:h_event 0x????????    line=u3,age=u8,id=0x??????,block[u1]={0x????????,...,0x????????}
    // #cache_mem:m_event 0x????????    line=u3,valid={u1,u1},age={u8,u8},id={0x??????,0x??????}
}

void evento_de_cache(CACHE *cache, int type_cache, FILE *output, uint32_t addr, uint32_t cache_index, uint32_t i)
{
    char *name_event;
    switch (type_cache)
    {
    case 1:
        // irm
        name_event = "irm";
        print_cache(cache, name_event, addr, output, 1, cache_index, i);
        break;
    case 2:
        // drm
        name_event = "drm";
        print_cache(cache, name_event, addr, output, 2, cache_index, i);
        break;
    case 3:
        // dwm
        name_event = "dwm";
        print_cache(cache, name_event, addr, output, 3, cache_index, i);
        break;
    case 4:
        // irh
        name_event = "irh";
        print_cache(cache, name_event, addr, output, 4, cache_index, i);
        break;
    case 5:
        // drh
        name_event = "drh";
        print_cache(cache, name_event, addr, output, 5, cache_index, i);
        break;
    case 6:
        // dwh
        name_event = "dwh";
        print_cache(cache, name_event, addr, output, 6, cache_index, i);
        break;
    case 7:
        // dwh (store_fault)
        name_event = "dwh";
        print_cache(cache, name_event, addr, output, 7, cache_index, i);
        break;
    case 8:
        // dwm (store_fault)
        name_event = "dwm";
        print_cache(cache, name_event, addr, output, 8, cache_index, i);
        break;
    default:
        printf("Colocou o numero errado ai manezao\n");
        break;
    }
};

void atualiza_lru(CACHE *cache)
{
    for (uint32_t i = 0; i < NUM_SETS; i++){
        for (uint32_t j = 0; j < ASSOCIATIVITY; j++)
        {
            if (cache->lines[i][j].valid)
            {
                if (cache->lines[i][j].age < MAX_AGE)
                    cache->lines[i][j].age++;
            }
        }
    }
}

uint32_t encontrar_lru(CACHE *cache, uint32_t cache_index)
{
    uint32_t via = 0;
    uint32_t maior_age = cache->lines[cache_index][0].age;

    for (uint32_t i = 1; i < cache->associativity; i++)
    {
        if (cache->lines[cache_index][i].age > maior_age)
        {
            maior_age = cache->lines[cache_index][i].age;
            via = i;
        }
    }
    return via;
}

void salvar_na_ram(uint8_t *mem, CACHELINE *linha, uint32_t index)
{
    uint32_t bits_offset = log2(BLOCK_SIZE);
    uint32_t bits_index = log2(NUM_SETS);
    uint32_t addr_fisico = 
    (linha->id << (bits_index + bits_offset)) |
    (index << bits_offset);
    if (addr_fisico < OFFSET_RAM ||
    addr_fisico + BLOCK_SIZE > MAX_RAM)
    {
        return;
    }

    uint32_t addr_ram = addr_fisico - OFFSET_RAM;
    uint32_t words = BLOCK_SIZE / 4;
    uint32_t base_idx = addr_ram >> 2;

    for (uint32_t i = 0; i < words; i++)
    {
        ((uint32_t *)mem)[base_idx + i] =
        ((uint32_t *)linha->data)[i];
    }
}

uint32_t acessar_cache(CACHE *cache, uint8_t *mem, uint32_t cache_index, uint32_t cache_tag, uint32_t addr, char info_cache[15], uint8_t bytes, uint32_t data, FILE *output, uint32_t info)
{
    cache->total_accesses++;
    uint32_t dado_lido = 0;
    int hit = 0;
    int id_hit, id_miss;
    bool is_store = false;

    if (strcmp(info_cache, "instruction") == 0)
    {
        id_hit = 4;
        id_miss = 1;
    }
    else if (strcmp(info_cache, "read") == 0)
    {
        id_hit = 5;
        id_miss = 2;
    }
    else if (strcmp(info_cache, "write") == 0)
    {
        if (info == 123456789){
            info = 0;
            id_hit = 7;
            id_miss = 8;
        }
        else {
            id_hit = 6;
            id_miss = 3;
        }
        is_store = true;
    }

    uint32_t via = 0xFFFFFFFF;
    for (uint32_t i = 0; i < cache->associativity; i++)
    {
        if (cache->lines[cache_index][i].valid && cache->lines[cache_index][i].id == cache_tag)
        {
            cache->hits++;
            uint8_t *bloco_bytes = (uint8_t *)cache->lines[cache_index][i].data;
            uint32_t base_line_addr = addr & (cache->block_size - 1); 
            
            for (int j = 0; j < (int)bytes; j++) {
                uint32_t current_addr = addr + j;
                uint32_t current_offset = current_addr & (cache->block_size - 1);
                
                uint8_t byte_temp; 

                if ((current_addr & ~(cache->block_size - 1)) == base_line_addr) {
                    if (is_store) {
                        // STORE: Pega os 8 bits menos significativos do dado deslocado
                        bloco_bytes[current_offset] = (uint8_t)((data >> (8 * j)) & 0xFF);
                        cache->lines[cache_index][i].dirty = 1;
                    } else {
                        byte_temp = bloco_bytes[current_offset];
                    }
                } else {
                    // RAM
                    uint32_t ram_idx = current_addr - OFFSET_RAM;
                    if (current_addr >= OFFSET_RAM && current_addr < MAX_RAM) {
                        if (is_store) {
                            mem[ram_idx] = (uint8_t)((data >> (8 * j)) & 0xFF);
                        } else{
                            byte_temp = mem[ram_idx];
                        }
                    } else {
                        byte_temp = 0; // Leitura fora da RAM retorna 0
                    }
                }

                // 2. CONSTRUÇÃO DO DADO (READ)
                if (!is_store) {
                        dado_lido |= ((uint32_t)byte_temp & 0xFF) << (8 * j);
                }
            }

            cache->lines[cache_index][i].age = 0;
            evento_de_cache(cache, id_hit, output, addr, cache_index, i);
            hit = 1;
            via = i;
            break;
        }
    }

    if (!hit)
    {
        for (uint32_t i = 0; i < cache->associativity; i++)
        {
            if (!cache->lines[cache_index][i].valid)
            {
                via = i;
                break;
            }
        }

        if (via == 0xFFFFFFFF)
        {
            via = encontrar_lru(cache, cache_index);
        }
        
        evento_de_cache(cache, id_miss, output, addr, cache_index, via); 
        if (is_store) 
        {
            for(int j = 0; j < (int)bytes; j++) {
                uint32_t current_addr = addr + j;
                if (current_addr >= OFFSET_RAM && current_addr < MAX_RAM) {
                    mem[current_addr - OFFSET_RAM] = (uint8_t)((data >> (8 * j)) & 0xFF);
                }
            }
            cache->lines[cache_index][via].age = 0;
            atualiza_lru(cache);
            
            return 0; 
        }
        
        if (cache->lines[cache_index][via].dirty && cache->lines[cache_index][via].valid){
            salvar_na_ram(mem, &cache->lines[cache_index][via], cache_index);
            cache->lines[cache_index][via].dirty = 0;
        }   
        cache->lines[cache_index][via].id = cache_tag;
        cache->lines[cache_index][via].valid = 1;
        cache->lines[cache_index][via].age = 0;
        
        uint32_t block_addr = (addr / cache->block_size) * cache->block_size;
        if (block_addr < OFFSET_RAM || block_addr + cache->block_size > MAX_RAM) {
            printf("ERRO: acesso fora da RAM: addr=0x%08x\n", addr);
            return -1;
        }
        else{
            uint32_t palavras = cache->block_size / 4;
            uint32_t base_idx = (block_addr - OFFSET_RAM) >> 2;
            for (uint32_t i = 0; i < palavras; i++)
            {
                ((uint32_t *)cache->lines[cache_index][via].data)[i] = ((uint32_t *)mem)[base_idx + i];
            }
        }

        if (!is_store) {
            uint8_t *bloco_bytes = (uint8_t *)cache->lines[cache_index][via].data;
            uint32_t base_line_addr = addr & ~(cache->block_size - 1);
        
            for (int j = 0; j < (int)bytes; j++) {
                uint32_t current_addr = addr + j;
                uint8_t byte_temp;

                if ((current_addr & ~(cache->block_size - 1)) == base_line_addr) {
                    byte_temp = bloco_bytes[current_addr & (cache->block_size - 1)];
                } else if (current_addr >= OFFSET_RAM && current_addr < MAX_RAM) {
                    byte_temp = mem[current_addr - OFFSET_RAM];
                } else {
                    byte_temp = 0;
                }

                dado_lido |= ((uint32_t)byte_temp & 0xFF) << (8 * j);
            }
        }
    }
    atualiza_lru(cache);
    return dado_lido;
};

void plic_w(PLIC_REG *plic, uint32_t addr, uint32_t value)
{
    uint32_t offset = addr - OFFSET_PLIC;
    // --- PRIORITY ---
    if (offset < 0x001000)
    {
        uint32_t id = offset / 4;
        if (id > 0 && id < PLIC_SOURCE_COUNT)
        {
            plic->priority[id] = value;
        }
        return;
    }

    // --- PENDING ---
    else if (offset >= 0x001000 && offset < 0x001080)
    {
        uint32_t idx = (offset - 0x001000) / 4;
        if (idx < (PLIC_SOURCE_COUNT / 32))
        {
            plic->pending[idx] = value;
        }
        return;
    }

    // --- ENABLE ---
    else if (offset >= 0x002000 && offset < 0x002080)
    {
        uint32_t idx = (offset - 0x002000) / 4;
        if (idx < (PLIC_SOURCE_COUNT / 32))
        {
            plic->enable[0][idx] = value;
        }
        return;
    }

    // --- THRESHOLD/CLAIM ---
    else if (offset >= 0x002000 && offset <= 0x200004)
    {
        uint32_t offset_reg = offset - 0x200000;
        // --- THRESHOLD ---
        if (offset_reg == 0)
        {
            plic->threshold[0] = value;
        }
        // --- CLAIM ---
        else if (offset_reg == 4)
        {
            if (value == plic->claim[0])
                plic->claim[0] = 0;
        }
        return;
    }
}

uint32_t plic_r(PLIC_REG *plic, uint32_t add)
{
    uint32_t offset = add - OFFSET_PLIC;
    // --- PRIORITY ---
    if (offset < 0x001000)
    {
        uint32_t id = offset / 4;
        if (id > 0 && id < PLIC_SOURCE_COUNT)
        {
            return plic->priority[id];
        }
        return 0;
    }

    // --- PENDING ---
    else if (offset >= 0x001000 && offset < 0x001080)
    {
        uint32_t idx = (offset - 0x001000) / 4;
        if (idx < (PLIC_SOURCE_COUNT / 32))
        {
            return plic->pending[idx];
        }
        return 0;
    }

    // --- ENABLE ---
    else if (offset >= 0x002000 && offset < 0x002080)
    {
        uint32_t idx = (offset - 0x002000) / 4;
        if (idx < (PLIC_SOURCE_COUNT / 32))
        {
            return plic->enable[0][idx];
        }
        return 0;
    }

    // --- THRESHOLD/CLAIM ---
    else if (offset >= 0x002000 && offset <= 0x200004)
    {
        uint32_t offset_reg = offset - 0x200000;
        // --- THRESHOLD ---
        if (offset_reg == 0)
        {
            return plic->threshold[0];
        }
        // --- CLAIM ---
        else if (offset_reg == 4)
        {
            uint32_t max_priority = 0;
            uint32_t best_id = 0;

            for (int i = 1; i < PLIC_SOURCE_COUNT; i++)
            {
                uint32_t idx = i / 32;
                uint32_t bit = i % 32;

                uint32_t is_pending = (plic->pending[idx] >> bit) & 1;
                uint32_t is_enable = (plic->enable[0][idx] >> bit) & 1;

                if (is_pending && is_enable)
                {
                    if (plic->priority[i] > max_priority)
                    {
                        max_priority = plic->priority[i];
                        best_id = i;
                    }
                }
            }

            if (max_priority < plic->threshold[0])
            {
                best_id = 0;
            }

            else if (best_id > 0)
            {
                plic->claim[0] = best_id;

                uint32_t idx = best_id / 32;
                uint32_t bit = best_id % 32;
                plic->pending[idx] &= ~(1 << bit);
            }
            return best_id;
        }
    }
    return 0;
}

uint32_t uart_r(uint32_t add, UART_REG *uart)
{
    switch (add)
    {
    case 0b000:
    {
        uint32_t data = uart->rhr;
        uart->lsr &= ~0x01;
        return data;
    }
    case 0b001:
        return uart->ier;
    case 0b010:
    {
        uint8_t iir = 0x01;

        if ((uart->lsr & 0x01) && (uart->ier & 0x01))
        {
            iir = 0x04;
        }
        return iir;
    }
    case 0b101:
        return uart->lsr | 0x60;
    default:
        return 0;
    }
};

void uart_w(UART_REG *uart, PLIC_REG *plic, uint32_t add, uint8_t data, FILE *term_out)
{
    switch (add)
    {
    case 0b000:
        putchar((char)data);
        fflush(stdout);

        if (term_out)
        {
            fputc((char)data, term_out);
            fflush(term_out);
        }
        uart->lsr |= 0x20;
        if (uart->ier & 0x02)
        {
            plic->pending[0] |= (1 << 10);
        }
        break;
    case 0b001:
        uart->ier = data;
        break;
    case 0b010:
        break;
    default:
        break;
    }
};

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
                       uint32_t tval)
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
    mstatus = (mstatus & ~(0b11 << 11)) | (0b11 << 11);
    ;

    // Escreve o valor MIE no MSTATUS
    csr_w(csr, 0x300, mstatus);
    // Le o endereco de destino no mtvec (0x305)
    uint32_t vetor = csr_r(0x305, csr);
    // Desviando fluxo: pc recebe o valor de mtvec
    *pc = (vetor & ~0x3);
}

void interruption_handler(CSR_REG *csr, uint32_t *pc, uint32_t cause)
{
    // MEPC recebe o codigo da proxima instrucao
    csr_w(csr, 0x341, *pc);
    // Atualizando MCAUSE (bit 31 = 1 para interrupcao)
    csr_w(csr, 0x342, cause | (1U << 31));

    uint32_t mstatus = csr_r(0x300, csr);
    uint32_t mie = (mstatus >> 3) & 1;

    mstatus = (mstatus & ~(1 << 7)) | (mie << 7);
    mstatus &= ~(1 << 3);
    mstatus = (mstatus & ~(0b11 << 11)) | (0b11 << 11);
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

uint32_t read_word(uint32_t addr, uint8_t *mem, uint8_t bytes, uint32_t index)
{
    if (bytes == 4)
    {
        uint32_t word = (uint32_t)mem[index] | (uint32_t)(mem[index + 1] << 8) |
                        (uint32_t)(mem[index + 2] << 16) |
                        (uint32_t)(mem[index + 3] << 24);
        return word;
    }
    else if (bytes == 2)
    {
        uint16_t word = *((uint16_t *)(mem + addr - OFFSET_RAM));
        return word;
    }
    else
    {
        uint8_t word = *((int8_t *)(mem + addr - OFFSET_RAM));
        return word;
    }
}

uint32_t load(PLIC_REG *plic, UART_REG *uart, uint32_t addr, uint64_t mtime, uint64_t mtimecmp,
              uint32_t msip)
{
    uint32_t index = 0;
    // 1. UART
    if (addr >= OFFSET_UART && addr <= MAX_UART)
    {
        index = (addr - OFFSET_UART) & 0x7;

        return uart_r(index, uart);
    }

    // 2. CLINT
    else if (addr >= OFFSET_CLINT && addr <= MAX_CLINT)
    {
        // --- SOFTWARE ---
        if (addr == OFFSET_CLINT)
        {
            return msip;
        }

        // --- TIMER ---
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
        return 0;
    }

    // 4. PLIC
    else if (addr >= OFFSET_PLIC && addr <= MAX_PLIC)
    {
        return plic_r(plic, addr);
    }

    // 5. RAM
    else if (addr >= OFFSET_RAM && addr <= MAX_RAM)
    {
        return 0;
    }

    return 0;
}

int store(CACHE *cache, PLIC_REG *plic, UART_REG *uart, uint8_t *mem, uint32_t cache_index, uint32_t cache_tag, uint8_t bytes, uint32_t addr, uint32_t data, uint64_t *mtimecmp, uint32_t *msip, FILE *term_out, FILE *output)
{
    // 1. UART
    if (addr >= OFFSET_UART && addr <= MAX_UART)
    {
        uint32_t uart_register = ((addr - OFFSET_UART) & 0x7);
        uart_w(uart, plic, uart_register, (uint8_t)data, term_out);
        return 0;
    }

    // 2. CLINT
    if (addr >= OFFSET_CLINT && addr <= MAX_CLINT)
    {
        // --- SOFTWARE ---
        if (addr == OFFSET_CLINT)
        {
            *msip = (data & 1);
            return 0;
        }

        // --- TIMER --
        if (addr == OFFSET_MTIMECMP)
        {
            *mtimecmp = (*mtimecmp & 0xFFFFFFFF00000000) | (uint64_t)data;
        }
        else if (addr == OFFSET_MTIMECMP + 4)
        {
            *mtimecmp = (*mtimecmp & 0x00000000FFFFFFFF) | ((uint64_t)data << 32);
        }
        return 0;
    }

    // 4. PLIC
    else if (addr >= OFFSET_PLIC && addr <= MAX_PLIC)
    {
        plic_w(plic, addr, data);
        return 0;
    }

    // 5. RAM
    if (addr >= OFFSET_RAM && (addr + bytes) < MAX_RAM)
    {
        uint32_t dado_lido = acessar_cache(cache, mem, cache_index, cache_tag, addr, "write", bytes, data, output, 0);
        return dado_lido;
    }

    return 123456789;
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
    sprintf(col2_inst, " ");
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
        else
            fprintf(output, "%-15s%-17s%s\n", col1_addr, col2_inst, col3_details);
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
        else
            fprintf(output, "%-18s%-19s%s\n", col1_addr, col2_inst, col3_details);
    }
}

void print_instruction(char *buffer, char *especific_instruction, char *col1_addr, char *col2_inst, uint32_t pc,
                       uint32_t rs1, uint32_t rs2, uint32_t rd, uint32_t imm,
                       char *instruction_name, char **x_label)
{
    char tipo = tolower(buffer[0]);
    sprintf(col1_addr, "0x%08x:%s", pc, instruction_name);
    if (tipo == 'r')
    {
        sprintf(col2_inst, "%s,%s,%s", x_label[rd], x_label[rs1], x_label[rs2]);
    }
    else if (tipo == 'i')
    {
        if (strcmp(especific_instruction, "print_imm") == 0)
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
    else if (tipo == 'c')
    {
        if (strcmp(especific_instruction, "nind") == 0)
        {
            sprintf(col2_inst, "");
        }
    }
    else if (tipo == 's')
    {
        sprintf(col2_inst, "%s,0x%03x(%s)", x_label[rs2], (uint16_t)(imm & 0xFFF), x_label[rs1]);
    }
    else if (tipo == 'b')
    {
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
        exit(1);
    };
    FILE *output = fopen(argv[2], "w");
    if (!output)
    {
        perror("Erro ao abrir arquivo de saida");
        fclose(input);
        exit(1);
    };
    FILE *term_in = NULL;
    if (argc >= 4)
    {
        term_in = fopen(argv[3], "r");
        if (!term_in)
        {
            perror("Erro ao abrir qemu.terminal.in");
        }
    }
    FILE *term_out = NULL;
    if (argc >= 5)
    {
        term_out = fopen(argv[4], "w");
        if (!term_out)
        {
            perror("Erro ao abrir qemu.terminal.out");
        }
    }
    init_log();
    
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
        return 1;
    }
    memset(mem, 0, MEM_SIZE);

    char token[32];
    uint32_t current_index = 0;
    uint32_t offset = 0;

    while (fscanf(input, "%31s", token) == 1)
    {
        if (token[0] == '@')
        {
            if (sscanf(token + 1, "%x", &offset) == 1)
            {
                uint32_t new_index = offset - OFFSET_RAM;
                current_index = new_index;
            }
            continue;
        }

        unsigned int valor;
        if (sscanf(token, "%x", &valor) == 1)
        {
            if (current_index < MEM_SIZE)
            {
                mem[current_index] = (uint8_t)valor;
                current_index++;
            }
        }
    }

    // Inicializando variáveis
    CACHE cache_i, cache_d;
    init_cache(&cache_d);
    init_cache(&cache_i);
    uint32_t bits_offset = log2(BLOCK_SIZE);
    uint32_t bits_index = log2(NUM_SETS);
    CSR_REG csr = {0};
    UART_REG uart = {0};
    PLIC_REG plic = {0};
    uint8_t run = 1;
    uint32_t pc = OFFSET_RAM;
    uint32_t x[32] = {0};
    uint32_t msip = 0;
    uint64_t mtime = 0, mtimecmp = -1;
    char col1_addr[40];
    char col2_inst[60];
    char col3_details[128];

    // Lendo arquivo term_in
    char *term_in_buffer = NULL;
    size_t term_in_size = 0;
    size_t term_in_pos = 0;

    if (term_in)
    {
        fseek(term_in, 0, SEEK_END);
        term_in_size = ftell(term_in);
        fseek(term_in, 0, SEEK_SET);
        term_in_buffer = malloc(term_in_size);
        fread(term_in_buffer, 1, term_in_size, term_in);
        fclose(term_in);
    }
    while (run)
    {
        // Verifica se instrucao esta na cache
        uint8_t bts = 0;
        uint32_t dados = 0;
        uint32_t cache_index = (pc >> bits_offset) & (NUM_SETS - 1);
        uint32_t cache_tag = pc >> (bits_offset + bits_index);
        
        // Incrementa o timer
        mtime++;
        if (mtime >= mtimecmp)
            csr.mip |= (1 << 7);
        else
            csr.mip &= ~(1 << 7);
        
        if (term_in_buffer && term_in_pos < term_in_size && !(uart.lsr & 0x01))
        {
            uart.rhr = (uint8_t)term_in_buffer[term_in_pos++];
            uart.lsr |= 0x01;
        }
        
        if ((uart.lsr & 0x01) && (uart.ier & 0x01))
        {
            plic.pending[0] |= (1 << 10);
        }
        
        // Verifica se o PLIC mandou o sinal
        bool plic_signal = ((plic.pending[0] & plic.enable[0][0]) && (plic.priority[10] > plic.threshold[0]));
        if (plic_signal)
        {
            csr.mip |= (1 << 11);
        }
        else
        {
            csr.mip &= ~(1 << 11);
        }
        
        // Verifica bit de interrupção de software
        if (msip & 1)
        csr.mip |= (1 << 3);
        else
        csr.mip &= ~(1 << 3);
        
        uint32_t mstatus = csr_r(0x300, &csr);
        uint32_t mie = csr_r(0x304, &csr);
        uint32_t mip = csr.mip;
        bool global_enable = (mstatus >> 3) & 1;
        
        // Verifica se há interrupção de hardware(UART)
        bool external_interrupt = global_enable && ((csr.mip & (1 << 11)) && (mie & (1 << 11)) && (mstatus & 0x08));
        if (external_interrupt)
        {
            interruption_handler(&csr, &pc, 11);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(1, mepc, mtval, mcause, "external", output);
            continue;
        }
        
        // Verifica se há interrupção de software
        bool soft_enable = (mie >> 3) & 1;
        bool soft_pending = (mip >> 3) & 1;
        bool soft_interrupt = global_enable && soft_enable && soft_pending;
        if (soft_interrupt)
        {
            interruption_handler(&csr, &pc, 3);
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
            interruption_handler(&csr, &pc, 7);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(1, mepc, mtval, mcause, "timer", output);
            continue;
        }
        
        uint32_t instructio = acessar_cache(&cache_i, mem, cache_index, cache_tag, pc, "instruction", bts, dados, output, 0);

        // Verifica se há exceção de instrução inválida
        if (pc >= MAX_RAM || pc < OFFSET_RAM || instructio == 0xFFFFFFFF)
        {
            exception_handler(&csr, &pc, 1, 0);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            print(0, mepc, mtval, mcause, "instruction_fault", output);
            continue;
        }
        
        // Fetch
        const uint32_t instruction = ((uint32_t *)(mem))[(pc - OFFSET_RAM) >> 2];
        const uint8_t opcode = instruction & 0b1111111;
        const uint8_t funct7 = instruction >> 25;
        const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
        // Imediato de 12 e 7 bits, tipos b e s, respectivamente
        const uint16_t imm = (instruction >> 20) & 0b111111111111;
        const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
        const uint16_t imm_b =  ((instruction >> 31) & 0b1) << 12 |
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
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "add",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b000 && funct7 == 0b0100000)
            {
                const int32_t data = (int32_t)x[rs1] - (int32_t)x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sub",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x-0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = (uint32_t)data;
            }
            else if (funct3 == 0b001 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] << x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sll",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x<<%u=0x%08x", x_label[rd], x[rs1],
                        x[rs2] & 0x1F, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "srl",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", x_label[rd], x[rs1],
                        x[rs2] & 0x1F, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sra",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>>%u=0x%08x", x_label[rd], x[rs1],
                        x[rs2] & 0x1F, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b010 && funct7 == 0b0000000)
            {
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "slt",
                                  (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b011 && funct7 == 0b0000000)
            {
                const uint32_t data = (x[rs1] < x[rs2]) ? 1 : 0;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "sltu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b110 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] | x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "or",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b111 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] & x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "and",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b100 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] ^ x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "xor",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b000 && funct7 == 0b0000001)
            {
                const int64_t data = (int32_t)x[rs1] * (int32_t)x[rs2];
                const int32_t data_low = (int32_t)(data);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mul",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_low);
                if (rd != 0)
                    x[rd] = (uint32_t)data_low;
            }
            else if (funct3 == 0b001 && funct7 == 0b0000001)
            {
                int64_t data = (int64_t)(int32_t)x[rs1] * (int64_t)(int32_t)x[rs2];
                uint32_t data_high = (uint32_t)(data >> 32);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulh",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
            }
            else if (funct3 == 0b010 && funct7 == 0b0000001)
            {
                const int64_t data =
                    (int64_t)(int32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                const int32_t data_high = (int32_t)(data >> 32);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulhsu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
                if (rd != 0)
                    x[rd] = (uint32_t)data_high;
            }
            else if (funct3 == 0b011 && funct7 == 0b0000001)
            {
                const uint64_t data =
                    (int64_t)(uint32_t)x[rs1] * (int64_t)(uint32_t)x[rs2];
                int32_t data_high = (uint32_t)(data >> 32);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "mulhu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x*0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data_high);
                if (rd != 0)
                    x[rd] = data_high;
            }
            else if (funct3 == 0b100 && funct7 == 0b0000001)
            {
                const uint32_t data =
                    (x[rs2] == 0) ? 0xFFFFFFFF
                                  : (uint32_t)((int32_t)x[rs1] / (int32_t)x[rs2]);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "div",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b101 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? 0xFFFFFFFF : x[rs1] / x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "divu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x/0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b110 && funct7 == 0b0000001)
            {
                const uint32_t data =
                    (x[rs2] == 0) ? x[rs1]
                                  : (uint32_t)((int32_t)x[rs1] % (int32_t)x[rs2]);
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "rem",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b111 && funct7 == 0b0000001)
            {
                const uint32_t data = (x[rs2] == 0) ? x[rs1] : x[rs1] % x[rs2];
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "remu",
                                  (char **)x_label);
                sprintf(col3_details, "%s=0x%08x%%0x%08x=0x%08x", x_label[rd], x[rs1],
                        x[rs2], data);
                if (rd != 0)
                    x[rd] = data;
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
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "slli", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x<<%u=0x%08x", x_label[rd], rs1_antigo, uimm,
                        data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b101 && funct7 == 0b0000000)
            {
                const uint32_t data = x[rs1] >> uimm;
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "srli", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>%u=0x%08x", x_label[rd], rs1_antigo, uimm,
                        data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b101 && funct7 == 0b0100000)
            {
                const uint32_t data = (int32_t)x[rs1] >> (int32_t)uimm;
                print_instruction(buffer_type, "print_imm", col1_addr, col2_inst, pc, rs1, rs2, rd, uimm, "srai", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x>>>%u=0x%08x", x_label[rd], rs1_antigo,
                        uimm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b010)
            {
                const uint32_t data = ((int32_t)x[rs1] < (int32_t)simm) ? 1 : 0;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "slti", (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b011)
            {
                const uint32_t data = (x[rs1] < (uint32_t)simm) ? 1 : 0;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sltiu", (char **)x_label);
                sprintf(col3_details, "%s=(0x%08x<0x%08x)=%u", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b100)
            {
                const uint32_t data = x[rs1] ^ simm;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "xori", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x^0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b111)
            {
                const uint32_t data = x[rs1] & simm;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "andi", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x&0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b110)
            {
                const uint32_t data = x[rs1] | simm;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "ori", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x|0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
            }
            else if (funct3 == 0b000)
            {
                const uint32_t data = x[rs1] + (int32_t)simm;
                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "addi", (char **)x_label);
                sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], rs1_antigo,
                        simm, data);
                if (rd != 0)
                    x[rd] = data;
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
            const int32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : (imm);
            const uint32_t address = x[rs1] + simm;
            
            uint32_t bytes;
            switch (funct3) {
                case 0b000: bytes = 1; break; // lb
                case 0b001: bytes = 2; break; // lh
                case 0b010: bytes = 4; break; // lw
            }

            char *buffer_type = "I";
            bool RAM = (address >= OFFSET_RAM && address + bytes <= MAX_RAM);
            bool CLINT = (address >= OFFSET_CLINT && address <= MAX_CLINT);
            bool UART = (address >= OFFSET_UART && address <= MAX_UART);
            bool PLIC = (address >= OFFSET_PLIC && address <= MAX_PLIC);
            bool is_valid = (RAM || UART || CLINT || PLIC);
            uint32_t dado_lido = 0;
            cache_index = (address >> bits_offset) & (NUM_SETS - 1);
            cache_tag = address >> (bits_offset + bits_index);
            if (RAM){
                dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
            }

            if (funct3 == 0b000)
            {
                int8_t data = 0;
                if (!is_valid)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address);
                    continue;
                }
                if (!RAM){
                    data = load(&plic, &uart, address, mtime, mtimecmp, msip);
                }

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lb", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, !data ? (int8_t)dado_lido : (int8_t)data);
                if (rd != 0)
                {
                    data = !data ? (int8_t)dado_lido : (int8_t)data;
                    x[rd] = (int32_t)data;
                }
            }
            else if (funct3 == 0b100)
            {
                uint8_t data = 0;
                if (!is_valid)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address);
                    continue;
                }

                if (!RAM) {
                    data = load(&plic, &uart, address, mtime, mtimecmp, msip);
                }

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lbu", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, (uint8_t)data ? (uint8_t)data : (uint8_t)dado_lido);
                if (rd != 0)
                {
                    data = (uint8_t)data ? (uint8_t)data : (uint8_t)dado_lido;
                    x[rd] = (uint8_t)data;
                }
            }
            else if (funct3 == 0b001)
            {
                int16_t data = 0;
                if (!is_valid)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address);
                    continue;
                }

                if (!RAM){
                    data = load(&plic, &uart, address, mtime, mtimecmp, msip);
                }

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lh", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data ? (int16_t)data : (int16_t)dado_lido);
                if (rd != 0){
                    data = data ? data : (int16_t)dado_lido;
                    x[rd] = (int32_t)data;
                }
            }
            else if (funct3 == 0b101)
            {
                uint16_t data = 0;
                if (!is_valid)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address);
                    continue;
                }

                if (!RAM)
                {
                    data = load(&plic, &uart, address, mtime, mtimecmp, msip);
                }

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, imm, "lhu", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data ? (uint16_t)data : (uint16_t)dado_lido);
                if (rd != 0){
                    data = data ? data : (uint16_t)dado_lido;
                    x[rd] = (uint32_t)data;
                }
            }
            else if (funct3 == 0b010)
            {
                uint32_t data = 0;
                if (!is_valid)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "read", bytes, dados, output, 0);
                    print(0, pc, address, 5, "load_fault", output);
                    exception_handler(&csr, &pc, 5, address);
                    continue;
                }

                if (!RAM) {
                    data = load(&plic, &uart, address, mtime, mtimecmp, msip);
                }

                print_instruction(buffer_type, "load", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "lw", (char **)x_label);
                sprintf(col3_details, "%s=mem[0x%08x]=0x%08x", x_label[rd], address, data ? data : dado_lido);
                if (rd != 0){
                    data = data ? data : dado_lido;
                    x[rd] = data;
                }
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
                
                const uint32_t previous = ((uint32_t *)(mem))[(pc - 4 - OFFSET_RAM) >> 2];
                const uint32_t next = ((uint32_t *)(mem))[(pc + 4 - OFFSET_RAM) >> 2];

                if (previous == 0x01f01013 && next == 0x40705013) {
                    cache_tag = (pc - 4) >> (bits_offset + bits_index);
                    cache_index = ((pc - 4) >> bits_offset) & (NUM_SETS - 1);
                    acessar_cache(&cache_i, mem, cache_index, cache_tag, pc - 4, "instruction", bts, dados, output, 0);
                    // atualiza_lru(&cache_i);
                    
                    cache_tag = (pc + 4) >> (bits_offset + bits_index);
                    cache_index = ((pc + 4) >> bits_offset) & (NUM_SETS - 1);
                    acessar_cache(&cache_i, mem, cache_index, cache_tag, pc + 4, "instruction", bts, dados, output, 0);
                    // atualiza_lru(&cache_i);
                    run = 0; 
                }
                else {
                    csr.mepc = pc;      // Salva PC atual
                    csr.mcause = 3;     // Causa = Breakpoint
                    pc = csr.mtvec;     // Pula para o Handler 
                }
                sprintf(col3_details, "");
            }
            // ECALL
            else if (imm == 0)
            {
                print_instruction(buffer_type, "nind", col1_addr, col2_inst, pc, rs1, rs2, rd, 0, "ecall", (char **)x_label);
                sprintf(col3_details, "");
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
                print(0, pc, 0, 11, "environment_call", output);
                exception_handler(&csr, &pc, 11, 0);
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
                mstatus &= ~(0b11 << 11);
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
                else
                {
                    new_data = csr_antigo;
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
                else
                {
                    new_data = csr_antigo;
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
            if (funct3 == 0b000 && imm == 1){
                fprintf(output, "%-18s%-20s%s", col1_addr, col2_inst, col3_details);
            }
            else {
                fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            }
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
            cache_index = (address >> bits_offset) & (NUM_SETS - 1);
            cache_tag = address >> (bits_offset + bits_index);
            uint32_t dado_lido = 0;

            if (funct3 == 0b000)
            {
                dados = store(&cache_d, &plic, &uart, mem, cache_index, cache_tag, 1, address, data_change, &mtimecmp, &msip, term_out, output);
                if (dados != 0)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "write", 1, 0, output, dados);
                    print(0, pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address);
                    continue;
                };

                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sb", (char **)x_label);
                sprintf(col3_details, "mem[0x%08x]=0x%02x", address, (uint8_t)data_change);
            }
            else if (funct3 == 0b001)
            {
                dados = store(&cache_d, &plic, &uart, mem, cache_index, cache_tag, 2, address, data_change, &mtimecmp, &msip, term_out, output);
                if (dados != 0)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "write", 2, 0, output, dados);
                    print(0, pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address);
                    continue;
                };

                print_instruction(buffer_type, "", col1_addr, col2_inst, pc, rs1, rs2, rd, simm, "sh", (char **)x_label);
                sprintf(col3_details, "mem[0x%08x]=0x%04x", address, (uint16_t)data_change);
            }
            else if (funct3 == 0b010)
            {
                dados = store(&cache_d, &plic, &uart, mem, cache_index, cache_tag, 4, address, data_change, &mtimecmp, &msip, term_out, output);
                if (dados != 0)
                {
                    dado_lido = acessar_cache(&cache_d, mem, cache_index, cache_tag, address, "write", 4, 0, output, dados);
                    print(0, pc, address, 7, "store_fault", output);
                    exception_handler(&csr, &pc, 7, address);
                    continue;
                };
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

            sprintf(col1_addr, "0x%08x:lui", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_a & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x", x_label[rd], data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            if (rd != 0)
                x[rd] = data;
            break;
        }
        case 0b0010111: // tipo U
        {
            const int32_t imm_u_val = (int32_t)(instruction & 0xFFFFF000);
            const uint32_t data = pc + imm_u_val;

            const uint32_t imm20_for_log = (uint32_t)imm_u_val >> 12;
            sprintf(col1_addr, "0x%08x:auipc", pc);
            sprintf(col2_inst, "%s,0x%05x", x_label[rd], imm20_for_log & 0xFFFFF);
            sprintf(col3_details, "%s=0x%08x+0x%08x=0x%08x", x_label[rd], pc,
                    imm_u_val, data);
            fprintf(output, "%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
            if (rd != 0)
                x[rd] = data;
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
            exception_handler(&csr, &pc, 2, instruction);
            uint32_t mcause = csr_r(0x342, &csr);
            uint32_t mepc = csr_r(0x341, &csr);
            uint32_t mtval = csr_r(0x343, &csr);
            print(0, mepc, mtval, mcause, "illegal_instruction", output);
            continue;
        }
        }
        pc += 4;
    }
    sprintf(col1_addr, "#cache_mem:dstats");
    sprintf(col2_inst, "");
    sprintf(col3_details, "hit=%.4f", cache_d.hits / cache_d.total_accesses);
    fprintf(output, "\n%-18s%-20s%s\n", col1_addr, col2_inst, col3_details);
    
    sprintf(col1_addr, "#cache_mem:istats");
    sprintf(col2_inst, "");
    sprintf(col3_details, "hit=%.4f", cache_i.hits / cache_i.total_accesses);
    fprintf(output, "%-18s%-20s%s", col1_addr, col2_inst, col3_details);
    
    free(mem);
    if (term_in_buffer)
        free(term_in_buffer);
    fclose(logi);
    fclose(term_out);
    fclose(output);
    fclose(input);
    printf("---------------------------------------------------------------------"
           "-----------\n");
    return 0;
}