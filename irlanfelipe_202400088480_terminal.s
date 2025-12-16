.text

# --- SCANF ---
scanf:
    getchar:
        # Base UART
        li t4, 0x10000000
        # Acumulador
        li a0, 0           
        # Flag de sinal
        li t1, 0

    # Econtrar o início do número (Pular espaços)
    esperar_digito:
        #Polling para esperar cadeia de números começar
        lb t0, 5(t4)
        andi t0, t0, 1
        beq t0, zero, esperar_digito 

        # Se passou, lê o char
        lb t0, 0(t4)
        
        # 32 na tabela ASCCI é: Espaço, Table ou Enter
        # Pula para esperar início do char caso seja < 32
        li t2, 32
        ble t0, t2, esperar_digito
        
        # Caso seja um sinal de "-" (ASCII = 45) ativa a flag e tenta ler o char
        li t2, 45
        bne t0, t2, atoi
        # Flag de negativo
        li t1, 1
        # Se leu sinal, tenta ler o próximo dígito
        j ler_digito_restante
    # Lógica da função ATOI (ASCII TO INT)
    atoi:
        # Se não foi '-', o char atual já é um dígito
        j calcular_digito

    # Lê o restante do número
    ler_digito_restante:
        # Verifica status
        lb t2, 5(t4)
        andi t2, t2, 1

        # Se não tem dado pronto (t2=0), assumimos que o número acabou.
        beq t2, zero, finalizar_leitura

        # Se tem dado, lê
        lb t0, 0(t4)

    calcular_digito:
        # Verifica se é dígito
        # Na tabela ASCII o dígito está entre 48(0) e 57(9)
        li t2, 48
        blt t0, t2, finalizar_leitura
        li t2, 57
        bgt t0, t2, finalizar_leitura

        # Lógica ATOI
        # Valor(t0) = char(t0) - 48
        addi t0, t0, -48
        # Total(a0) = (total(a0) * 10(t3)) + valor(t0)
        li t3, 10
        mul a0, a0, t3
        add a0, a0, t0

        # Continua lendo
        j ler_digito_restante

    finalizar_leitura:
        # Se a flag estiver desativada, retorna o número
        # Se não, converte o número negativo em positivo
        beq t1, zero, retornar_numero
        sub a0, zero, a0
    retornar_numero:
        ret

# PUTCHAR E ITOA, guardam toda a lógica do printf
# --- PUTCHAR ---
putchar:
    li t4, 0x10000000
    sb a0, 0(t4)
    ret

# --- ITOA ---
itoa: 
    addi sp, sp, -16
    sw ra, 0(sp)
    
    bne a0, zero, checar_se_negativo
    # Caso seja 0, imprime de uma vez
    li a0, 48
    call putchar
    j finalizar_itoa     
checar_se_negativo:
    # Se for > 0, imprime os dígitos
    bge a0, zero, converte
    # Se não, imprime sinal de menos e transforma dígito em positivo
    mv t0, a0          
    li a0, 45
    call putchar
    sub a0, zero, t0    
converte:
    # Declara contador e denominador da lógica ITOA
    li t0, 0            
    li t2, 10       
empilhar:
    beq a0, zero, desempilhar
    # Aqui mora a lógica ITOA (INT TO ASCCI) 
    # Dividimos por 10 sucessivamente
    # Guardamos o resto na pilha(sp) por que saem ao contrário
    rem t1, a0, t2      
    div a0, a0, t2      
    addi t1, t1, 48     
    addi sp, sp, -4
    sw t1, 0(sp)
    addi t0, t0, 1
    j empilhar
desempilhar: 
    beq t0, zero, finalizar_itoa
    # Como já somamos 48 anteriormente para virar ASCII
    # Apenas desempilhamos e chamamos a função PUTCHAR  
    lw a0, 0(sp)
    addi sp, sp, 4
    call putchar
    addi t0, t0, -1
    j desempilhar
finalizar_itoa:
    lw ra, 0(sp)
    addi sp, sp, 16
    ret

# --- heapify ---
heapify:
heapify_loop:
    mv t0, a2           # maior(a2) = i(t0)
    slli t5, a2, 2      
    add t5, a0, t5      
    lw t6, 0(t5)        # V[i]
    
    slli t1, a2, 1
    addi t1, t1, 1      # Esquerda
    addi t2, t1, 1      # Direita

    # Checar esquerda
    bge t1, a1, checar_direita
    slli t3, t1, 2      
    add t3, a0, t3
    lw t4, 0(t3)        # V[esquerda]
    ble t4, t6, checar_direita 
    mv t0, t1           
    mv t6, t4           

checar_direita:
    # Checar direita
    bge t2, a1, checar_troca
    slli t3, t2, 2      
    add t3, a0, t3
    lw t4, 0(t3)        # V[Direita]
    ble t4, t6, checar_troca
    mv t0, t2           

checar_troca:
    beq t0, a2, fim_heapify
    slli t3, a2, 2      
    add t3, a0, t3      
    lw t4, 0(t3)        # V[i]
    slli t5, t0, 2      
    add t5, a0, t5      
    lw t6, 0(t5)        # V[maior]
    sw t6, 0(t3)
    sw t4, 0(t5)
    mv a2, t0
    j heapify_loop

fim_heapify:
    ret

# --- heapsort ---
heapsort:
    # Inicializar
    addi sp, sp, -16
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    sw s2, 12(sp)

    mv s0, a0
    mv s1, a1
    
    # Constrói o Heap
    # i(s2) = (n(s1) / 2) - 1
    srli s2, s1, 1      
    addi s2, s2, -1

loop_construir:
    blt s2, zero, extrair_comeco
    mv a0, s0
    mv a1, s1
    mv a2, s2
    call heapify
    addi s2, s2, -1
    j loop_construir

extrair_comeco:
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

.globl main
main:
    addi sp, sp, -16
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)

    # ---> Ler N
    call scanf
    mv s0, a0
    mv s2, a0
    la s1, input

    beq s0, zero, fim

    # ---> Ler Vetor
loop_leitura: 
    beq s0, zero, do_sort
    call scanf
    sw a0, 0(s1)
    addi s1, s1, 4
    addi s0, s0, -1
    j loop_leitura

do_sort:
    # ---> Ordenar
    li t0, 1
    ble s2, t0, do_print
    la a0, input
    mv a1, s2
    call heapsort

do_print:
    # ---> Imprimir vetor ordenado
    la s1, input
    mv s0, s2

loop_wr:
    beq s0, zero, fim
    lw a0, 0(s1)
    call itoa
    addi s1, s1, 4
    addi s0, s0, -1
    beq s0, zero, fim
    li a0, 44           # Imprime ','
    call putchar
    j loop_wr

fim:
    # ---> Finaliza programa
    lw s1, 8(sp)
    lw s0, 4(sp)
    lw ra, 0(sp)
    addi sp, sp, 16
    li a0, 0
    ret

.data
input:
    .space 4000
    