# Relatório — Módulo Manchester e Temporização (Kroda)

## 1. Visão Geral da Implementação

A codificação Manchester e o mecanismo de temporização formaram as pedras angulares para garantir a robustez desse projeto em condições adversas de iluminação. Este relatório detalha como nossa equipe abandonou as abordagens clássicas de Timers baseados em interrupção para uma FSM (Máquina de Estados Finita) baseada em Polling e Atraso Deslocado, que provou ser substancialmente mais imune às lentidões elétricas do LDR.

---

## 2. Temporização: O Abandono de Interrupções e o Método "Shifted Delay"

### 2.1 O Problema Inicial
No início do projeto, usávamos a biblioteca `TimerOne` para cravar leituras a cada 50ms. No entanto, o `analogRead()` dentro (ou acionado por flag) de uma ISR criava flutuações. Mais criticamente: interrupções fixas assumem que o hardware de recepção mudou de estado quase instantaneamente após a luz ligar, o que é falso para o LDR (cuja resistência demora a baixar).

### 2.2 Sincronismo Automático (Auto-Baud)
Implementamos uma técnica de sincronização manual genial usando as funções nativas do Arduino:
1. O Transmissor envia um "Pulso Master" que dura **exatamente 5 vezes o tempo de bit** (`delay(tempoBit * 5)`).
2. O Receptor acorda, usa o `millis()` na borda de subida, e bloqueia num `while` até a borda de descida.
3. A largura temporal dessa luz dividida por 5 resulta no **tempo real e dinâmico do bit da rede**, permitindo suportar qualquer velocidade do emissor instantaneamente.

### 2.3 A técnica "Shifted Delay"
Para não ler ruídos de rampa (quando o LDR ainda está "acordando" ou "apagando"), o algoritmo pula o início do período:
```cpp
// Pula para o meio do primeiro bit
delay(tempoBit + (tempoBit / 2)); 
```
A partir desse deslocamento de 150%, todas as invocações de `delay(tempoBit)` caem magicamente no **centro exato** de cada bit subsequente.

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

Logo, o RX simplesmente usa um deslocamento maior no primeiro sincronismo para ignorar totalmente a primeira metade:
```cpp
if (modoCodificacao == 3) {
    delay(tempoBit + (tempoBit * 3 / 4)); // Cai na segunda metade lógica
}
```
Assim, a rotina genérica do NRZ-L aproveitada logo em seguida consegue ler a mensagem perfeitamente de forma idêntica à codificação base, mas desfrutando da ausência de DC imposta pelas transições Manchester no ar.

---

## 4. Vantagens Finais Observadas

1. **Eficiência no Uso da RAM:** Ao abolir o buffer temporal longo e decodificar os bits em fluxo (stream), o uso de memória não escalona com o tamanho da mensagem.
2. **Desacoplamento Universal:** O Transmissor e o Receptor não importam absolutamente nenhuma dependência de hardware além do núcleo do Arduino. O código é 100% C++.
3. **Resistência ao Degrau de Luz:** Essa mecânica temporal unida ao **Forward Error Correction (Redundância bit a bit)** tornou o canal virtualmente invulnerável a mãos passadas na frente da luz por micro-segundos.