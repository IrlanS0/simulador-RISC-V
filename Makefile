# --- Configuração Portátil ---
CC = gcc
CFLAGS = -Wall -g -std=c99

# --- Detecção de Sistema (A Mágica) ---
EXECUTABLE = main
# Assume comandos Unix por padrão
RM = rm -f
RM_DIR = rm -rf
MKDIR_CMD = @mkdir -p $(OUTPUT_DIR)
EXEC_PREFIX = ./

# Se o 'make' detectar que está no Windows...
ifeq ($(OS),Windows_NT)
	EXECUTABLE := $(EXECUTABLE).exe
	RM = del /f /q
	RM_DIR = rmdir /s /q
	MKDIR_CMD = @mkdir $(OUTPUT_DIR) 2>NUL || exit 0
	EXEC_PREFIX = .\
else
	EXEC_PREFIX = ./
endif
# Fim do bloco ifeq

# --- Arquivos do Projeto ---
OUTPUT_DIR = build

# --- MUDANÇA: Definir TODOS os arquivos .c e .h ---
SRCS = src/irlanfelipe_20240008480_poximv1.c
# OBJS = $(SRCS:.c=.o)
# HDRS = structs.h hash_table.h mergesort.h

# --- Descoberta Automática de Testes ---
INPUTS = $(wildcard test/*.hex)
OUTPUTS = $(patsubst test/%.hex, $(OUTPUT_DIR)/%.out, $(INPUTS))

# --- "Receitas" (Targets) ---
.PHONY: all test clean

all: $(EXECUTABLE)

# --- MUDANÇA: Regra de LINKAGEM ---
# O executável depende dos arquivos .o
$(EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# --- NOVO: Regra de COMPILAÇÃO (Pattern Rule) ---
# Como transformar CADA .c em um .o
# Se qualquer header (HDRS) mudar, recompila o .o
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Regra de Teste (Sem mudanças, mas depende de 'all') ---
test: all $(OUTPUTS)
	@echo "--- Todos os testes foram gerados! ---"

# Esta regra está perfeita
$(OUTPUT_DIR)/%.out: test/%.txt $(EXECUTABLE)
	$(MKDIR_CMD)
	@echo "Rodando teste: $< ..."
	$(EXEC_PREFIX)$(EXECUTABLE) $< $@

# --- MUDANÇA: Limpar os arquivos .o também ---
clean:
	@echo "Limpando..."
	@-del /f /q $(EXECUTABLE)
	@-del /f /q src\*.o
	@-rmdir /s /q $(OUTPUT_DIR)
	@echo "Limpo!"