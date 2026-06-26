# Projeto de Comunicação de Dados Óptica (LED + LDR)

Bem-vindos ao repositório oficial do projeto de Comunicação de Dados (UTFPR). Este projeto implementa um link de comunicação simplex por luz visível utilizando a plataforma Arduino Uno.

O projeto foi arquitetado focado em lidar com as restrições físicas do **LDR** (que possui inércia química/lentidão para transitar do claro para o escuro).

## 🚀 Status Atual (Fase 1 Concluída)
Eu (Felipe) estruturei o repositório, consolidei a arquitetura e implementei a primeira versão funcional (**Mínimo Produto Viável**). 
O código atual (na branch principal) já possui:
- Calibração dinâmica do LDR no setup do Receptor (criando um Limiar de claro/escuro dinâmico, sem hardcode).
- Uso da biblioteca `TimerOne` para cravar a base de tempo, evitando `delay()` no envio de dados.
- Módulo `CRC-16-CCITT` completo e validado em C++.
- Montagem do Frame completo (Preâmbulo de 4 bytes, SFD, Cabeçalho inteligente).
- Envio e Recepção funcional usando a codificação base **NRZ-L**.

---

## 🏗️ Arquitetura de Hardware (Bancada)
A montagem física que assumimos para que o código rode perfeitamente:

- **Transmissor (TX):** 
  - 1 Arduino Uno + 1 LED de alto brilho. 
  - O **Ânodo do LED** está ligado no **Pino Digital 8**.
  - O **Cátodo** está ligado num **Resistor de 330 Ω** e depois ao GND.
- **Receptor (RX):** 
  - 1 Arduino Uno + 1 LDR genérico + Resistor 3,3 kΩ.
  - LDR ligado ao **5V**. 
  - O meio do LDR encontra o Resistor de 3,3 kΩ. Deste **nó central** sai a leitura para o **Pino Analógico A0**. O outro lado do resistor vai ao GND (Divisor de Tensão pull-down).

---

## 📜 Entendendo o Protocolo criado
Nós criamos um protocolo em camadas robusto. Todos os frames enviados pelo TX contêm:

1. **Preâmbulo (4 bytes):** Enviado sempre em NRZ-L. Sequência de `10101010` para "acordar" o receptor e estabilizar a química do LDR.
2. **SFD (Start Frame Delimiter):** Enviado sempre em NRZ-L. Um byte `11110000` que marca o fim do sincronismo.
3. **Cabeçalho (1 byte):** Enviado sempre em NRZ-L. 
   - 2 bits informando a codificação do Payload (`00`=NRZ-L, `01`=NRZ-I, `10`=Manchester).
   - 6 bits informando o tamanho da mensagem (vai de `000000` a `111111`, que matematicamente significa `Tamanho Real - 1`). Suporta mensagens de 1 a 64 bytes.
4. **Payload (Dados da Mensagem):** Enviado usando a codificação sinalizada no cabeçalho.
5. **CRC-16 (2 bytes):** Enviado usando a codificação do cabeçalho. É calculado sobre o Cabeçalho + Payload.

> **Importante:** A taxa de bits (Baud Rate) foi fixada em **20 bps (50 ms por bit)** para o NRZ, devido à lentidão extrema de decaimento do LDR. No Manchester, a taxa base precisará ser reduzida ainda mais (5 a 10 bps).

---

## 📋 Divisão de Tarefas (Próximos Passos)

Para finalizarmos o projeto e conseguirmos a nota bônus, precisamos integrar o Sincronismo Automático, o NRZ-I e o Manchester (conforme definido nas restrições da arquitetura). A divisão oficial de tarefas ficou definida assim:

### 👨‍💻 Felipe (Gerente de Projeto e Integração)
**Foco:** Hardware, Sincronismo e Validação Final.
- **Tarefa 1:** Codificar o Sincronismo Inicial Inteligente (Auto-Baud) no receptor. A rotina aguardará os picos do preâmbulo (usando `micros()`) para calcular o verdadeiro período do pulso na hora, em vez de assumir os 50 ms estáticos.
- **Tarefa 2:** Revisar *Pull Requests*, unir os módulos da equipe, testar a distorção luminosa na bancada (LDR com capa preta) e documentar as evidências no Serial Plotter.
- **Tarefa 3:** Gravação do vídeo e envio dos fontes finais.

### 👨‍💻 Elder (Especialista NRZ-I)
**Foco:** Lógica com Memória de Estado de Bit.
- O código base atual usa NRZ-L (0 e 1 literais). O Elder deve estudar e criar as rotinas de comutação do NRZ-I.
- **Tarefa 1 (TX):** Na rotina de interrupção do Transmissor, o NRZ-I precisará verificar o último nível físico da linha (herdadado do cabeçalho enviado em NRZ-L) e usá-lo como "estado base" para iniciar as inversões do payload.
- **Tarefa 2 (RX):** Alterar o switch/case do `processa_byte_recebido` para conseguir decodificar as tensões de LDR lidas transformando mudança-de-estado em 1, e não-mudança em 0.
- **Tarefa 3:** Escrever para o Relatório o descritivo de "por que o NRZ-I é melhor ou pior que o NRZ-L numa transmissão por LDR" baseado nos seus testes práticos ou de simulação de mesa.

### 👨‍💻 Kroda (Especialista Manchester)
**Foco:** Manipulação de Temporizadores e Alta Frequência.
- O bônus máximo. O Manchester não apenas muda estados, ele exige que se mude o estado no MEIO do bit.
- **Tarefa 1 (TX e RX):** Como o Manchester precisa de duas fatias de tempo, a sua rotina deverá dizer pro Timer base dobrar de velocidade (ler/enviar a cada meio bit) unicamente durante o processamento do Payload.
- **Tarefa 2 (Decodificação):** Se recebeu 1 e depois 0 -> decodifica como `0`. Se recebeu 0 e depois 1 -> decodifica como `1`.
- **Tarefa 3:** Escrever a justificativa pro relatório explicando porque tivemos que diminuir o Baud rate do Manchester para 10 bps e explicar a imunidade de transições do protocolo.

---

## 📚 Documentação Técnica (Novo)
Para que não falte absolutamente nada para a equipe e tudo flua usando as melhores práticas:
1. Leiam o documento [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): Ele explica **o porquê** tomamos cada uma das decisões do projeto (como o atraso do LDR, a decisão de usar NRZ-L fixo no cabeçalho e a justificativa para o uso de timers ao invés de delays).
2. Leiam o documento [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md): Ele explica o fluxo de **GitHub Flow** que vamos utilizar, como abrir Pull Requests e as regras de ouro para não travar a interrupção do Arduino na hora de commitar seus trechos de código.

---

## 🛠️ Como Clonar e Executar

1. Clone o repositório na sua máquina:
   `git clone https://github.com/felipe-rodriguesz/Projeto-ComunicacaoDeDados.git`
2. Abra a Arduino IDE e certifique-se de instalar a biblioteca genérica **TimerOne** pelo Gerenciador de Bibliotecas.
3. Carregue `src/tx_arduino/tx_arduino.ino` em um Uno, e `src/rx_arduino/rx_arduino.ino` em outro Uno. 
4. Ao dar energia no RX, deixe a área em repouso por 3 segundos, e depois ilumine o LDR com o LED fixo por 3 segundos para calibrar o limiar.
5. Use os Monitores Seriais (baud 9600) para enviar strings!
