# 🏥 Sistema Integrado de Gestão Hospitalar (Sistemas Operativos)

[cite_start]Este projeto simula o fluxo completo de um hospital, desde a entrada de emergências até à conclusão de cirurgias, utilizando uma arquitetura multiprocessada e multithreaded em **C**. 

### ⚙️ Arquitetura e Conceitos Implementados
* [cite_start]**Gestão de Processos**: Utilização de `fork()` para criar módulos independentes (Triagem, Cirurgia, Farmácia, Laboratório) geridos por um Gestor Central[cite: 45, 48].
* **Comunicação Inter-Processos (IPC)**:
    * [cite_start]**System V Message Queues**: Para o encaminhamento de mensagens prioritárias entre módulos[cite: 35, 37].
    * [cite_start]**Named Pipes (FIFOs)**: Para o processamento de comandos de texto (input_pipe) e interações externas[cite: 29, 35].
    * [cite_start]**Shared Memory**: Memória partilhada protegida por **Mutexes** para estatísticas em tempo real[cite: 60, 64].
* [cite_start]**Sincronização Avançada**: Uso de **Semáforos POSIX** para gestão de recursos escassos como salas de cirurgia e equipas médicas[cite: 66].
* [cite_start]**Tratamento de Sinais**: Implementação de `sigaction` para garantir um `graceful shutdown` (encerramento limpo) de todos os recursos do sistema[cite: 45, 52].

### [cite_start]📂 Estrutura do Projeto [cite: 29]
* `src/`: Implementação de todos os módulos.
* `include/`: Definições de protocolos de comunicação e structs.
* `config/`: Parâmetros de simulação configuráveis (config.txt).
* `results/`: Snapshots estatísticos e relatórios automáticos.

### 📊 Avaliação Técnica
* **Nota Final**: 17/20 (84%).
* **Destaque**: Implementação robusta de mecanismos de sincronização e tratamento de sinais POSIX.

---
*Projeto desenvolvido para a unidade curricular de Sistemas Operativos - Engenharia Informática @ IPV*
