# Sistema Integrado de Gestão Hospitalar

Projeto desenvolvido no âmbito da unidade curricular de **Sistemas Operativos**, com o objetivo de aplicar conceitos de processos, threads, comunicação inter-processos (IPC), sincronização e gestão de recursos partilhados, através da simulação de um sistema hospitalar.

---

## 1. Requisitos

- Sistema Operativo Linux (ou WSL)
- Compilador GCC
- Biblioteca POSIX Threads (`pthread`)
- Permissões para criação de named pipes (FIFO)

Nota: O sistema deve ser executado num sistema de ficheiros nativo Linux, pois pastas partilhadas podem apresentar limitações no funcionamento de pipes POSIX.

---

## 2. Estrutura do Projeto
hospital_system/
├── src/ # Código-fonte
├── include/ # Headers
├── config/
│ └── config.txt # Ficheiro de configuração
├── logs/
│ └── hospital_log.txt # Logs do sistema
├── results/
│ ├── lab_results/ # Resultados laboratoriais
│ ├── pharmacy_deliveries/# Entregas da farmácia
│ └── stats_snapshots/ # Snapshots estatísticos
├── sample_commands/ # Ficheiros de comandos de teste
├── Makefile
└── README.md

---

## 3. Comandos disponíveis
Os comandos devem ser enviados através do input_pipe.
Exemplo: echo 'EMERGENCY PAC701 3 500' > input_pipe

3.1 Emergências
Formato:
EMERGENCY <ID> <TRIAGE> <STABILITY>

Exemplo:
echo "EMERGENCY PAC001 3 500" > input_pipe

3.2 Consultas Programadas
Formato:
APPOINTMENT <ID> <TRIAGE> <STABILITY> [SCHEDULED]

Exemplos:
echo "APPOINTMENT PAC101 4 600" > input_pipe
echo "APPOINTMENT PAC102 5 700 50" > input_pipe

3.3 Estatísticas do Sistema
Formato:
STATUS

Exemplo:
echo "STATUS" > input_pipe
