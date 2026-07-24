# 💻 Poxim-V — Simulador RISC-V em C

[![C99](https://img.shields.io/badge/C-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()

Simulador educacional da arquitetura **RISC-V (RV32I + extensão M)** desenvolvido em C.  
O projeto executa instruções passo a passo, simulando aspectos reais de hardware como **cache, exceções, interrupções** e geração de *trace* detalhado da execução.

---

## 🚀 Visão Geral

O **Poxim-V** é capaz de:

- 📥 Carregar programas em formato hexadecimal (`.hex`)
- ⚙️ Executar instrução por instrução
- 📊 Gerar um *trace* completo da execução (`.out`)
- 🧠 Simular comportamento de CPU com recursos avançados

---

## 🧠 Arquitetura do Projeto

O simulador foi projetado de forma modular, separando responsabilidades principais:

- **CPU** → Decodificação e execução das instruções  
- **Memória** → Gerenciamento da memória principal  
- **Cache** → Simulação de cache de instruções e dados  
- **Handler** → Tratamento de exceções e interrupções  
- **IO Serial** → Simulação de comunicação serial  

---

## ⚙️ Funcionalidades

### 🔹 Execução & Trace
- Execução passo a passo de instruções RISC-V com controle de PC.
- Geração de arquivo `.out` detalhado contendo:
  - Endereço da instrução e desmontagem Assembly.
  - Estado dos registradores e da memória cache.
  - Métricas de *Hit/Miss* para cache de instruções e dados.

### 🔹 Simulação de Hardware
- Caches dedicadas de instruções e dados.
- Comunicação serial por arquivo (Entrada/Saída de terminal).

### 🔹 Exceções & Interrupções
- **Exceções:** `load fault`, `store fault` e `illegal instruction`.
- **Interrupções:** Timer, Software, External e suporte a CLINT (*Core Local Interruptor*).

---

## 📂 Estrutura de Pastas

```text
.
├── src/            # Código-fonte em C
├── test/           # Casos de teste (.hex e .in do terminal)
├── out/            # Arquivos gerados durante os testes (.out)
├── Makefile        # Scripts de compilação e testes
└── README.md
```

## 🛠️ Como Usar

### 📌 Pré-requisitos
- Compilador C com suporte ao padrão C99 (gcc)
- Utility make

## 🔧 Compilação e Testes

### Compilar o executável
`make`
### Executar a suíte de testes automática
`make test`
### Limpar arquivos compilados e saídas de teste
`make clean`

## 💻 Execução Manual
### Para rodar um programa hexadecimal específico manualmente:
`./index <codigo.hex> <saida.out> <terminal.in> <terminal.out>`

### Exemplo:
`./index test/input2.hex out/input2.out test/terminal.in out/terminal.out`

---