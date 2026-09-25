# Relatório — Módulo Manchester e Temporização (Kroda)

## 1. Visão Geral da Implementação

A codificação Manchester e o mecanismo de temporização foram partes importantes deste projeto. Este relatório registra a implementação da equipe, que substituiu a abordagem inicial com Timer por leitura bloqueante baseada em polling e atraso deslocado. A recepção atual é sequencial e bloqueante; não implementa uma FSM explícita.

---

## 2. Temporização: O Abandono de Interrupções e o Método "Shifted Delay"

### 2.1 O Problema Inicial
No início do projeto, usávamos a biblioteca `TimerOne` para cravar leituras a cada 50ms. No entanto, o `analogRead()` dentro (ou acionado por flag) de uma ISR criava flutuações. Mais criticamente: interrupções fixas assumem que o hardware de recepção mudou de estado quase instantaneamente após a luz ligar, o que é falso para o LDR (cuja resistência demora a baixar).

### 2.2 Sincronismo Automático (Auto-Baud)
O mecanismo de sincronização usa funções nativas do Arduino:
1. O transmissor envia um pulso de calibração com duração definida como cinco tempos de bit (`delay(tempoBit * 5)`).
2. O receptor detecta o nível por polling e mede a duração com `millis()`.
3. A duração medida é dividida por cinco para estimar o tempo de bit. A estimativa tem resolução limitada e depende do polling e da resposta do LDR; não implica suporte a qualquer velocidade.

### 2.3 A técnica "Shifted Delay"
Para não ler ruídos de rampa (quando o LDR ainda está "acordando" ou "apagando"), o algoritmo pula o início do período:
```cpp
// Pula para o meio do primeiro bit
delay(tempoBit + (tempoBit / 2)); 
```
A partir desse deslocamento, as leituras seguintes avançam em intervalos de `tempoBit`. O instante real de amostragem depende da detecção do início e da resposta do canal óptico.

---

## 3. Codificação Manchester: A Abordagem Assíncrona

### 3.1 Geração (Transmissão)
Na codificação Manchester padrão (IEEE 802.3), um bit '1' gera uma transição de Baixo para Alto, e um bit '0' gera de Alto para Baixo.
A implementação final no Transmissor não usa buffers de expansão complexos. Ela modula os pinos diretamente na hora do envio de forma estrutural:
```cpp
if (bitAtual == 1) {
    digitalWrite(PINO_LED, LOW);
    delay(tempoBit / 2);
    digitalWrite(PINO_LED, HIGH);
    delay(tempoBit / 2);
} else { ... }
```
Isso impõe as duas transições necessárias dividindo perfeitamente a base de tempo.

### 3.2 Decodificação Simplificada (O Pulo de 75%)
Enquanto algoritmos acadêmicos tentam rastrear o momento exato da inversão no meio do bit (que costuma falhar devido ao LDR), nossa decodificação aborda o Manchester matematicamente:
No Manchester, o **segundo semi-período do bit contém o exato nível lógico do dado original**.
Se foi `0` (Alto→Baixo), a segunda metade é `Baixo` (0).
Se foi `1` (Baixo→Alto), a segunda metade é `Alto` (1).

Na implementação atual, o RX usa um deslocamento maior antes da primeira amostragem para ler na segunda metade do primeiro bit de dados:
```cpp
if (modoCodificacao == 3) {
    delay(tempoBit + (tempoBit * 3 / 4)); // Cai na segunda metade lógica
}
```
Assim, a rotina de amostragem por nível também é usada para os dados Manchester. O RX não valida explicitamente a transição central. Start e stop são níveis convencionais, sem codificação Manchester. O resultado depende da temporização e do comportamento do canal óptico.

---

## 4. Vantagens Finais Observadas

1. **Leitura em fluxo:** A recepção monta os bytes durante a leitura, sem armazenar um buffer temporal longo.
2. **Dependências:** Os sketches usam as funções do núcleo Arduino e não incluem bibliotecas externas.
3. **Observação de bancada:** O comportamento físico depende da montagem e das condições do canal. O FEC reduz determinados erros nas representações do payload, mas não torna o enlace imune a interrupções ou ruído.
