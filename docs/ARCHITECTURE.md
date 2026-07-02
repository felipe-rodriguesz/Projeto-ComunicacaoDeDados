# Arquitetura e Decisões de Projeto

Este documento detalha o "porquê" das escolhas técnicas assumidas para a implementação da comunicação óptica por LED e LDR neste projeto da disciplina de Comunicação de Dados. O objetivo é que toda a equipe tenha um referencial único para justificar as abordagens técnicas no relatório final.

## 1. Restrições Físicas e Hardware

### O Problema da Inércia Química do LDR
O LDR (Resistor Dependente de Luz) não é um semicondutor instantâneo como um fototransistor. Ele possui **alta inércia química**. O tempo de subida (detectar luz) é razoavelmente rápido, mas o tempo de descida (detectar a escuridão) pode levar dezenas de milissegundos.
*   **Decisão:** Flexibilizar a taxa de transmissão, permitindo controle dinâmico do usuário no TX, mas recomendando taxas baixas (>= 50ms por bit) para garantir estabilidade. O Manchester, especialmente, precisa de janelas maiores devido às transições de meio-bit.

### Limiar (Threshold) Simples e Robusto
*   **Decisão:** Diferente das calibrações dinâmicas exageradas de setups anteriores, fixamos um limiar seguro de `400` no RX para definir a divisão entre claro e escuro.
*   **Impacto:** Permite detecção imediata com menos overhead matemático durante a operação.

---

## 2. Sincronismo Inicial (Auto-Baud) - (Garante Bônus de 0.5)

Para que o RX não seja escravo de um "Baud Rate" hardcoded que poderia dessincronizar devido às imperfeições dos cristais osciladores do Arduino, foi implementado um **Sincronismo Automático por Tempo**.
*   **Decisão:** O TX envia um pulso ALTO de calibração que dura exatamente **5 tempos de bit**. O RX detecta a borda de subida usando um laço `while` bloqueante e marca o tempo de início com `millis()`. Ao detectar a borda de descida, marca o tempo final.
*   **A Matemática:** O tempo do bit do RX é definido simplesmente dividindo a duração total desse pulso por 5.
*   **Impacto:** Sincronismo perfeito sem recorrer a bibliotecas de interrupções temporizadas pesadas.

---

## 3. Estratégia de Leitura e Delay Deslocado

Muitos projetos acadêmicos tentam usar interrupções de hardware (como a antiga versão que usava `TimerOne`), mas descobriram que a latência e o overhead das ISRs geram problemas de timing no Arduino.
*   **Decisão:** Nossa arquitetura final utiliza um polling estratégico e bloqueante via `delay()`.
*   **O Truque Central:** Para evitar ler nos momentos instáveis em que a luz está acendendo ou apagando, o `lerByte()` dá um delay inicial de `(tempoBit + tempoBit/2)`. Isso empurra a primeira amostragem matematicamente para o **centro exato** da janela do sinal luminoso do primeiro dado, saltando a transição.
*   **Variante Manchester:** Como o Manchester concentra sua informação lógica na última transição, o atraso é empurrado para o 3/4 da janela usando `(tempoBit + (tempoBit * 3/4))`.

---

## 4. Codificações Físicas Suportadas

O protocolo incorpora perfeitamente as 3 lógicas diretamente no envio/recepção serial:
*   **NRZ-L:** O valor do bit é o valor físico (1=Luz, 0=Apagado).
*   **NRZ-I:** O valor do bit determina a *mudança* (1 inverte o estado anterior, 0 mantém). Implementado no RX checando `luzAtual != estadoAnterior`.
*   **Manchester:** Transições forçadas no meio do bit. Decodificado de forma engenhosa apenas atrasando a amostragem para a segunda metade lógica do bit.

---

## 5. Proteção de Dados e Forward Error Correction (FEC) - (Garante Bônus de 0.5)

Um LDR ao ar livre está muito suscetível a erros de rajada por conta de sombras ou reflexos não intencionais. Para não termos que exigir retransmissão de dados, implementamos um sistema de **Correção Ativa**.

### Votação de Maioria a Nível de Bit
*   **O Algoritmo no TX:** Ao invés de enviar `1 byte` de dados, enviamos `3 bytes` redundantes: o original, o complemento bit a bit (`~dado`), e uma máscara de alternância lógica (`dado ^ 0xAA`).
*   **O Algoritmo no RX:** O receptor reconstrói as máscaras usando lógica reversa e soma os votos de todos os três pacotes **bit a bit**. Se um ruído inverter um dos bits de luz em um dos bytes da sequência, os outros dois o sobrepõem por maioria simples (2 contra 1).
*   **Impacto:** O sistema é capaz de reconstruir magicamente até **3 erros isolados ou contínuos por pacote** antes mesmo de verificar a integridade final.

### Validação de Integridade Final
*   Para atestar que o pacote recuperado pelo FEC está verdadeiramente perfeito (Requisito obrigatório do professor), utilizamos um algoritmo iterativo nativo de **CRC-8**.
*   Ele substituiu nossa antiga biblioteca CCITT-16 externa, tornando o código autossuficiente e economizando tempo de banda no ar.