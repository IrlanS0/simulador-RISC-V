# --- Configuração de Compilação ---
CC = gcc
CFLAGS = -Wall -g -std=c99
EXECUTABLE = index
OUTPUT_DIR = out

# --- Detecção de Sistema Operacional ---
ifeq ($(OS),Windows_NT)
    RM = del /f /q
    RM_DIR = rmdir /s /q
    EXEC_PREFIX = 
    FIX_PATH = $(subst /,\,$1)
else
    RM = rm -f
    RM_DIR = rm -rf
    EXEC_PREFIX = ./
    FIX_PATH = $1
endif

MKDIR_CMD = @mkdir -p $(OUTPUT_DIR)

# --- Arquivos do Projeto ---
SRC = src/index.c

# --- Descoberta Automática de Testes ---
# Mapeia todos os testes a partir dos arquivos .hex
INPUTS = $(wildcard test/*.hex)
OUTPUTS = $(patsubst test/%.hex, $(OUTPUT_DIR)/%.out, $(INPUTS))

# --- Alvos (.PHONY) ---
.PHONY: all test clean

all: $(EXECUTABLE)

# --- Regra de Linkagem ---
$(EXECUTABLE): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(EXECUTABLE)

# --- Regra de Testes ---
test: all $(OUTPUTS)
	@echo "--- Todos os testes foram gerados! ---"

# --- REGRA DE EXECUÇÃO ---
# Passa 4 argumentos:
# 1. test/NOME.hex
# 2. out/NOME.out
# 3. test/NOME_terminal.in  (ou test/terminal.in)
# 4. out/NOME_terminal.out (ou out/terminal.out)
$(OUTPUT_DIR)/%.out: test/%.hex $(EXECUTABLE)
	$(MKDIR_CMD)
	@echo "Rodando teste: $< ..."
	$(EXEC_PREFIX)$(EXECUTABLE) $< $@ test/$*_terminal.in $(OUTPUT_DIR)/$*_terminal.out

# --- Limpeza ---
clean:
	@echo "Limpando..."
	@$(RM) $(EXECUTABLE)
	@$(RM_DIR) $(OUTPUT_DIR)
	@echo "Limpo!"