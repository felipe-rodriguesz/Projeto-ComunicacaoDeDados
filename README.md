# Projeto de Comunicação de Dados Óptica (LED + LDR)

Bem-vindos ao repositório oficial do projeto de Comunicação de Dados (UTFPR). Este projeto implementa um link de comunicação simplex por luz visível utilizando a plataforma Arduino Uno.

O projeto foi arquitetado focado em lidar com as restrições físicas do **LDR** (que possui inércia química/lentidão para transitar do claro para o escuro).

## 🚀 Status Atual (Projeto Concluído - Versão Master)
A equipe estruturou e consolidou a arquitetura final. O código atual atende 100% dos requisitos obrigatórios e alcançou a **pontuação máxima nos bônus**.
O código na branch principal possui:
- Sincronismo Automático (Auto-Baud) implementado de forma nativa através da medição de pulsos com `millis()`.
- Algoritmo nativo de **CRC-8** embutido no código, garantindo validação de integridade.
- **Forward Error Correction (FEC)** ativo através de codificação de repetição e decodificação por Votação de Maioria Estendida a nível de bit (corrige até 3 bits errados automaticamente).
- Envio e Recepção funcional e dinâmico com suporte às três codificações exigidas: **NRZ-L, NRZ-I e Manchester**.
- Código 100% nativo (sem uso de bibliotecas de terceiros como `TimerOne`), garantindo máxima portabilidade.

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
Nós criamos um protocolo em camadas extremamente robusto, com correção ativa. Todos os frames enviados pelo TX contêm:

1. **Preâmbulo de Calibração Automática:** O TX emite um pulso longo de luz correspondente a 5 tempos de bit. O RX usa isso para definir o baud rate dinamicamente.
2. **Tamanho do Payload (1 byte):** Enviado informando o tamanho real da mensagem.
3. **Payload Protegido (FEC):** Para cada byte da mensagem real, o transmissor envia **3 bytes** (O original, o original invertido, e uma máscara de alternância).
4. **CRC-8 (1 byte):** É calculado sobre o Payload puro para atestar que, mesmo após a Correção de Erros, os dados finais estão íntegros.

---

## 📁 Estrutura do Repositório e Organização dos Arquivos

Para que todos saibam onde alterar o código e onde ler a documentação, o repositório foi organizado da seguinte forma:

```text
/
├── README.md               # Este arquivo principal com o resumo e arquitetura base.
├── docs/                   # Pasta dedicada a toda a documentação teórica do projeto.
│   ├── ARCHITECTURE.md     # Documento explicando o PORQUÊ tomamos cada decisão.
│   ├── CONTRIBUTING.md     # Regras de ouro de código e passos do GitHub.
│   └── kroda_relatorio.md  # Relatório específico do módulo de Manchester e temporização.
└── src/                    # Pasta que contém os códigos-fonte C++ que rodam nas placas.
    ├── tx_arduino/         # Projeto da placa Emissora.
    │   └── tx_arduino.ino  # Script mestre do Transmissor (Codificações, FEC e CRC).
    └── rx_arduino/         # Projeto da placa Receptora.
        └── rx_arduino.ino  # Script mestre do Receptor (Auto-baud, Votação de Maioria e CRC).
```

### O que tem nos códigos fonte?
- **`tx_arduino.ino`:** Script interativo. Ele pergunta ao usuário pelo Monitor Serial a velocidade, a codificação (NRZ-L, NRZ-I ou Manchester) e a mensagem. Em seguida, gera o frame protegido por redundância, calcula o CRC-8 e transmite modulando o pino digital.
- **`rx_arduino.ino`:** Aguarda em estado inativo a recepção de luz. Ao receber o pulso, afere o tempo para sincronismo (Auto-baud). Em seguida, efetua a leitura bloqueante e dessincronizada de todos os bytes, executando em tempo real o algoritmo de Votação de Maioria para corrigir eventuais ruídos do LDR antes de validar o CRC-8.

---

## 📚 Documentação Técnica (Leitura Obrigatória)
Para que não falte absolutamente nada para a equipe e tudo flua usando as melhores práticas:
1. Leiam o documento [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): Ele explica as brilhantes decisões de software, como o Forward Error Correction e o Auto-baud via `millis()`.
2. Leiam o documento [docs/kroda_relatorio.md](docs/kroda_relatorio.md): Relatório focado nas minúcias temporais da implementação do Manchester.

---

## 🛠️ Como Clonar e Executar

1. Clone o repositório na sua máquina.
2. Abra a Arduino IDE (nenhuma biblioteca externa é necessária).
3. Carregue `src/tx_arduino/tx_arduino.ino` no emissor. 
4. Carregue `src/rx_arduino/rx_arduino.ino` no receptor.
5. Abra o Monitor Serial do RX em **115200 baud** e escolha a codificação.
6. Abra o Monitor Serial do TX em **115200 baud**, digite a taxa desejada (ex: 50), escolha a codificação correspondente e insira a mensagem.
