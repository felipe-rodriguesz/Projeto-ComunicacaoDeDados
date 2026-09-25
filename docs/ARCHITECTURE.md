# Arquitetura e Decisões de Projeto

Este documento detalha o "porquê" das escolhas técnicas assumidas para a implementação da comunicação óptica por LED e LDR neste projeto da disciplina de Comunicação de Dados. O objetivo é que toda a equipe tenha um referencial único para justificar as abordagens técnicas no relatório final.

## 1. Restrições Físicas e Hardware

### O Problema da Inércia Química do LDR
O LDR responde às mudanças de iluminação com atraso, o que afeta o instante e a forma das transições observadas pelo RX.
*   **Decisão:** O TX permite informar o tempo de bit. O código não valida um intervalo mínimo ou máximo; a escolha precisa considerar a resposta do LDR e a temporização do enlace. No Manchester, cada bit de dados ocupa duas metades.

### Limiar (Threshold) Simples e Robusto
*   **Decisão:** Diferente das calibrações dinâmicas exageradas de setups anteriores, fixamos um limiar seguro de `400` no RX para definir a divisão entre claro e escuro.
*   **Impacto:** O RX compara as leituras com esse valor fixo; não há calibração dinâmica do limiar.

---

## 2. Sincronismo Inicial (Auto-Baud)

O enlace usa um pulso de calibração para estimar o tempo de bit no RX.
*   **Decisão:** O TX mantém o LED em nível alto durante cinco tempos de bit. O RX detecta as transições por polling, registra os instantes com `millis()` e divide a duração medida por cinco.
*   **Estimativa:** O resultado é uma estimativa em milissegundos por bit, com resolução limitada pela medição em milissegundos, pelo polling e pelas transições observadas no LDR.
*   **Limites:** O procedimento não negocia o modo de codificação. TX e RX precisam estar configurados no mesmo modo.

---

## 3. Estratégia de Leitura e Delay Deslocado

Muitos projetos acadêmicos tentam usar interrupções de hardware (como a antiga versão que usava `TimerOne`), mas descobriram que a latência e o overhead das ISRs geram problemas de timing no Arduino.
*   **Decisão:** Nossa arquitetura final utiliza um polling estratégico e bloqueante via `delay()`.
*   **Amostragem NRZ:** `lerByte()` usa um atraso inicial de `(tempoBit + tempoBit/2)` antes de amostrar os dados. O instante efetivo depende da detecção do start bit e da temporização do canal.
*   **Variante Manchester:** O atraso inicial é `(tempoBit + (tempoBit * 3/4))`, posicionando a leitura na segunda metade do primeiro bit de dados; as leituras seguintes avançam em intervalos de um tempo de bit.

---

## 4. Codificações Físicas Suportadas

O protocolo implementa três modos de codificação no envio e na recepção serial:
*   **NRZ-L:** O valor do bit é o valor físico (1=Luz, 0=Apagado).
*   **NRZ-I:** O valor do bit determina a *mudança* (1 inverte o estado anterior, 0 mantém). Implementado no RX checando `luzAtual != estadoAnterior`.
*   **Manchester:** O TX força uma transição entre as metades de cada bit de dados. O RX usa uma estratégia específica: amostra o nível na segunda metade, sem validar explicitamente a transição central. Start e stop são níveis convencionais e não usam Manchester. A decodificação depende da temporização e do comportamento do canal óptico; não é um decodificador Manchester genérico.

---

## 5. Proteção de Dados e Forward Error Correction (FEC)

O payload inclui redundância por byte, que o RX processa com votação majoritária por posição de bit. O protocolo não implementa retransmissão.

### Votação de Maioria a Nível de Bit
*   **O Algoritmo no TX:** Ao invés de enviar `1 byte` de dados, enviamos `3 bytes` redundantes: o original, o complemento bit a bit (`~dado`), e uma máscara de alternância lógica (`dado ^ 0xAA`).
*   **O Algoritmo no RX:** O TX envia `d`, `~d` e `d ^ 0xAA`. O RX desfaz as duas transformações e vota entre as três representações lógicas de cada posição de bit. Para uma posição, a maioria tolera uma representação incorreta. Erros em duas ou três representações da mesma posição podem produzir um bit reconstruído incorreto; não há garantia geral de corrigir até três bits errados.
*   **Escopo:** A redundância protege os bytes do payload, não o tamanho, o CRC ou os bits de start/stop.

### Validação de Integridade Final
*   TX e RX calculam CRC-8 sobre o texto original e reconstruído, respectivamente, usando o polinômio `0x07` e o valor inicial `0x00`.
*   O CRC indica divergência entre o valor recebido e o calculado, mas não corrige dados. Não cobre o tamanho nem as representações redundantes. A implementação substituiu a antiga dependência CRC-16 externa.
