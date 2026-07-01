# Arquitetura e Decisões de Projeto

Este documento detalha o "porquê" das escolhas técnicas assumidas para a implementação da comunicação óptica por LED e LDR neste projeto da disciplina de Comunicação de Dados. O objetivo é que toda a equipe tenha um referencial único para justificar as abordagens técnicas no relatório e na arguição.

## 1. Restrições Físicas e Hardware

### O Problema da Inércia Química do LDR
O LDR (Resistor Dependente de Luz) não é um semicondutor instantâneo como um fototransistor. Ele possui **alta inércia química**. O tempo de subida (detectar luz) é razoavelmente rápido, mas o tempo de descida (detectar a escuridão) pode levar dezenas de milissegundos.
*   **Decisão:** Limitar drasticamente a taxa de transmissão. Fixou-se **20 bps (bits por segundo)** para a base NRZ e entre **5 a 10 bps** para o Manchester (já que o Manchester exige 2 transições no mesmo tempo de 1 bit).
*   **Impacto:** Se enviássemos em alta velocidade, as bordas do sinal borrariam e o Arduino leria lixo (ruído constante).

### Divisor de Tensão e Limiar (Threshold) Dinâmico
Não podíamos assumir um valor estático (ex: `analogRead > 500`) porque a luz da sala de aula ou projetores podem alterar esse nível a qualquer momento.
*   **Decisão:** O receptor tira uma média móvel de `analogRead()` do ambiente totalmente apagado (repouso) e depois totalmente aceso (fase de calibração no setup). O Limiar que decide se o bit é 1 ou 0 é sempre $Limiar = (Min + Max) / 2$.
*   **Impacto:** Resistência às interferências externas no momento exato do ensaio físico.

---

## 2. Decisões de Arquitetura de Software

### Timer vs Delay
Usar `delay()` bloqueia o processador e faz com que a recepção perca milissegundos críticos, tirando o momento da amostragem do "centro" do bit.
*   **Decisão:** Uso obrigatório da biblioteca `TimerOne`. O timer de hardware do ATmega328P garantirá que o TX envie os bits de forma matematicamente cravada e que o RX seja acordado no instante perfeito para amostrar.

### Interrupções e Leitura Analógica (Flag)
Embora usemos timers, o ADC (Conversor Analógico Digital) do Arduino é lento (~100µs) e bloqueante. 
*   **Decisão:** A Interrupção (ISR) do RX não chama o `analogRead()`. Ela apenas sobe uma bandeira (Flag `volatile bool flag_ler_adc = true`). O laço `loop()` detecta essa flag, lê o sensor rapidamente, faz uma pequena média e abaixa a flag.
*   **Impacto:** A CPU nunca trava as interrupções de tempo real.

### Prevenção de Fragmentação de Memória
A classe `String` do C++ fragmenta brutalmente os parcos 2KB de memória RAM do Arduino Uno.
*   **Decisão:** Manipulação de ponteiros e vetores primitivos (`char buffer[65]`) foi adotada de ponta a ponta.

---

## 3. Protocolo e Enquadramento

### Cabeçalho Híbrido Fixo (NRZ-L)
Como o receptor precisa saber qual decodificador usar para processar a mensagem (NRZ-L, NRZ-I ou Manchester), enviar o cabeçalho usando uma das 3 é o clássico dilema "o ovo ou a galinha".
*   **Decisão:** O Preâmbulo, o SFD (Start Frame Delimiter) e o Cabeçalho serão **SEMPRE** transmitidos e lidos em NRZ-L puro. Somente o **Payload (Dados)** e o **CRC** que o acompanha é que comutarão a lógica de software para NRZ-I ou Manchester.

### Codificação do Tamanho
Temos limite de 64 bytes de payload estipulado no projeto, mas precisamos encodar isso junto com o Tipo (2 bits) em **apenas 1 byte de Header**. Sobraram 6 bits (que vão de 0 a 63). 
*   **Decisão:** O valor do Payload será enviado subtraído de 1. O RX somará 1 na chegada. Assim representamos de 1 até 64 bytes.

### Controle de Erros (CRC-16-CCITT)
Para mensagens de 64 caracteres, um CRC-8 é estatisticamente falho. 
*   **Decisão:** Adotamos o algoritmo industrial CRC-16 (Polinômio `0x1021`), o qual garante taxa de detecção de erros simples/duplos de 100% no nosso pacote minúsculo.

---

## 4. Sincronismo Inicial (Auto-Baud)
O preâmbulo foi estendido para 4 bytes (`10101010` x4). 
*   **Decisão (Próxima Tarefa):** O RX não assumirá que o TX está enviando a 50ms exatos (devido às tolerâncias dos cristais das placas e atraso da luz). O RX anotará via `micros()` cada transição do preâmbulo e tirará a média matemática desse tempo na hora para inferir em qual "Baud Rate" real ele deve programar seu próprio `Timer1` para ouvir o restante do pacote.

---

## 5. Codificação NRZ-I (Payload/CRC)

### Por que NRZ-I e como foi implementado
O NRZ-I é uma codificação **diferencial**: em vez de o nível do LED representar o valor do bit diretamente (como no NRZ-L), o bit 1 significa "inverte o estado da linha" e o bit 0 significa "mantém o estado da linha".

*   **Decisão:** O Preâmbulo, o SFD e o Cabeçalho continuam sempre em NRZ-L puro (48 bits fixos), pois o receptor precisa decodificar essa parte sem ainda saber qual codificação foi escolhida. Somente o Payload e o CRC comutam para NRZ-I quando selecionado no cabeçalho.
*   **TX:** a interrupção de transmissão (`isr_transmite_bit`) mantém uma variável de estado (`nivel_linha_anterior`) com o último nível físico enviado. Ao entrar no payload em NRZ-I, essa variável já contém o último nível do cabeçalho (NRZ-L), servindo como ponto de partida coerente para as inversões.
*   **RX:** o receptor compara cada nova leitura do LDR com a leitura anterior (`nivel_fisico_anterior`). Mudança de nível decodifica como bit 1; nível mantido decodifica como bit 0.
*   **[Preencher depois de testar na bancada]:** comparação prática entre NRZ-I e NRZ-L quanto à robustez frente à inércia do LDR — sequências com muitos bits 1 geram mais transições de estado, o que pode ser mais sensível à lentidão de decaimento do sensor do que o NRZ-L.