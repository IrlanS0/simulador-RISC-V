.text
.global main

# ==============================================================================
# 1. FUNÇÕES DE IO (CORREÇÃO DO LOOP INFINITO NO FIM DO ARQUIVO)
# ==============================================================================

# --- GETCHAR ---
getchar:
    li t4, 0x10000000   # Base UART
    li a0, 0            # Acumulador
    li t1, 0            # Sinal

# 1. ESPERA BLOQUEANTE (Para encontrar o início do número)
wait_start:
    lb t0, 5(t4)
    andi t0, t0, 1
    beq t0, zero, wait_start  # Aqui TEM que esperar
    
    lb t0, 0(t4)        # Lê char
    
    li t2, 32
    ble t0, t2, wait_start
    
    li t2, 45
    bne t0, t2, process_digit
    li t1, 1            # Negativo
    # Se leu sinal, tenta ler o próximo dígito
    j try_read_digit

process_digit:
    # Se não foi '-', o char atual já é um dígito
    j calc_digit_logic

# 2. ESPERA NÃO-BLOQUEANTE (Para ler o restante do número)
try_read_digit:
    # Verifica status
    lb t2, 5(t4)
    andi t2, t2, 1
    
    # CORREÇÃO CRÍTICA:
    # Se não tem dado pronto (status=0) AGORA, assumimos que o número acabou.
    # (Resolve o bug do arquivo sem Enter no final)
    beq t2, zero, finish_read
    
    # Se tem dado, lê
    lb t0, 0(t4)

calc_digit_logic:
    # Verifica se é dígito
    li t2, 48
    blt t0, t2, finish_read
    li t2, 57
    bgt t0, t2, finish_read

    # Acumula
    addi t0, t0, -48
    li t3, 10
    mul a0, a0, t3
    add a0, a0, t0
    
    # Continua lendo (mas agora em modo não-bloqueante)
    j try_read_digit

finish_read:
    beq t1, zero, ret_get
    sub a0, zero, a0
ret_get:
    ret

# --- PUTCHAR (Escrita Turbo) ---
putchar:
    li t4, 0x10000000
    sb a0, 0(t4)
    ret

# --- ITOA ---
itoa: 
    addi sp, sp, -16
    sw ra, 0(sp)
    
    bne a0, zero, check_neg_i
    li a0, 48
    call putchar
    j itoa_end     
check_neg_i:
    bge a0, zero, conv_i
    mv t0, a0           
    li a0, 45
    call putchar
    sub a0, zero, t0    
conv_i:
    li t0, 0            
    li t2, 10       
push_d:
    beq a0, zero, pop_d
    rem t1, a0, t2      
    div a0, a0, t2      
    addi t1, t1, 48     
    addi sp, sp, -4
    sw t1, 0(sp)
    addi t0, t0, 1
    j push_d
pop_d: 
    beq t0, zero, itoa_end
    lw a0, 0(sp)
    addi sp, sp, 4
    call putchar
    addi t0, t0, -1
    j pop_d
itoa_end:
    lw ra, 0(sp)
    addi sp, sp, 16
    ret

# ==============================================================================
# 2. HEAPSORT (Seguro e Rápido)
# ==============================================================================

# --- heapify ---
heapify:
heapify_loop:
    mv t0, a2           # largest = i
    slli t5, a2, 2      
    add t5, a0, t5      
    lw t6, 0(t5)        # V[i]
    
    slli t1, a2, 1
    addi t1, t1, 1      # left
    addi t2, t1, 1      # right

    # Check Left
    bge t1, a1, check_right
    slli t3, t1, 2      
    add t3, a0, t3
    lw t4, 0(t3)        # V[left]
    ble t4, t6, check_right 
    mv t0, t1           
    mv t6, t4           

check_right:
    # Check Right
    bge t2, a1, check_swap
    slli t3, t2, 2      
    add t3, a0, t3
    lw t4, 0(t3)        # V[right]
    ble t4, t6, check_swap
    mv t0, t2           

check_swap:
    beq t0, a2, heapify_end_ret
    slli t3, a2, 2      
    add t3, a0, t3      
    lw t4, 0(t3)        # V[i]
    slli t5, t0, 2      
    add t5, a0, t5      
    lw t6, 0(t5)        # V[largest]
    sw t6, 0(t3)
    sw t4, 0(t5)
    mv a2, t0
    j heapify_loop

heapify_end_ret:
    ret

# --- heapsort ---
heapsort:
    addi sp, sp, -16
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    sw s2, 12(sp)

    mv s0, a0
    mv s1, a1
    
    # 1. Build Heap
    srli s2, s1, 1      # i = n / 2
    addi s2, s2, -1

build_loop:
    blt s2, zero, extract_start
    mv a0, s0
    mv a1, s1
    mv a2, s2
    call heapify
    addi s2, s2, -1
    j build_loop

    # 2. Extract
extract_start:
    addi s2, s1, -1     # i = n - 1

extract_loop:
    ble s2, zero, sort_done
    lw t0, 0(s0)        # V[0]
    slli t1, s2, 2      
    add t1, s0, t1
    lw t2, 0(t1)        # V[i]
    sw t2, 0(s0)
    sw t0, 0(t1)

    mv a0, s0
    mv a1, s2           
    li a2, 0            
    call heapify
    addi s2, s2, -1
    j extract_loop

sort_done:
    lw s2, 12(sp)
    lw s1, 8(sp)
    lw s0, 4(sp)
    lw ra, 0(sp)
    addi sp, sp, 16
    ret

# ==============================================================================
# 3. MAIN
# ==============================================================================
main:
    addi sp, sp, -16
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)

    # 1. Ler N
    call getchar
    mv s0, a0
    mv s2, a0
    la s1, input

    beq s0, zero, fim_main

    # 2. Ler Vetor
loop_rd: 
    beq s0, zero, do_sort
    call getchar
    sw a0, 0(s1)
    addi s1, s1, 4
    addi s0, s0, -1
    j loop_rd

do_sort:
    # 3. Heapsort
    li t0, 1
    ble s2, t0, do_print
    la a0, input
    mv a1, s2
    call heapsort

do_print:
    # 4. Imprimir
    la s1, input
    mv s0, s2

loop_wr:
    beq s0, zero, fim_main
    lw a0, 0(s1)
    call itoa
    addi s1, s1, 4
    addi s0, s0, -1
    beq s0, zero, fim_main
    li a0, 44           # ','
    call putchar
    j loop_wr

fim_main:
    lw s1, 8(sp)
    lw s0, 4(sp)
    lw ra, 0(sp)
    addi sp, sp, 16
    li a0, 0
    ret

.section .data
input:
    .zero 4000
    