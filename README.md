# 🏥 Sistema Integrado de Gestão Hospitalar (Sistemas Operativos)

Este projeto simula o fluxo completo de um hospital, desde a entrada de emergências até à conclusão de cirurgias, utilizando uma arquitetura multiprocessada e multithreaded em **C**. 

### ⚙️ Arquitetura e Conceitos Implementados
* **Gestão de Processos**: Utilização de `fork()` para criar módulos independentes (Triagem, Cirurgia, Farmácia, Laboratório) geridos por um Gestor Central.
* **Comunicação Inter-Processos (IPC)**:
    * **System V Message Queues**: Para o encaminhamento de mensagens prioritárias entre módulos.
    * **Named Pipes (FIFOs)**: Para o processamento de comandos de texto (input_pipe).
    * **Shared Memory**: Memória partilhada protegida por **Mutexes** para estatísticas em tempo real.
* **Sincronização Avançada**: Uso de **Semáforos POSIX** para gestão de recursos escassos como salas de cirurgia e equipas médicas.
* **Tratamento de Sinais**: Implementação de `sigaction` para garantir um `graceful shutdown` (encerramento limpo) de todos os recursos.

### 📊 Avaliação Técnica
* **Nota Final**: 17/20 (84%).
* **Destaque**: Implementação robusta de mecanismos de sincronização e tratamento de sinais POSIX.

---
*Projeto desenvolvido para a unidade curricular de Sistemas Operativos - Engenharia Informática @ IPV*
