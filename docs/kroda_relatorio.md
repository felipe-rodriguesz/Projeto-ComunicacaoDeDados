# Relatório — Módulo Manchester (Kroda)

## 1. Visão Geral da Implementação

A codificação Manchester foi integrada ao projeto como terceira opção de codificação (valor `10` nos 2 bits superiores do cabeçalho). O preâmbulo, SFD e cabeçalho continuam sendo enviados em NRZ-L (decisão de arquitetura do Felipe), e a codificação Manchester é aplicada exclusivamente ao **Payload** e ao **CRC**.

---

## 2. Convenção Adotada

Seguimos a convenção IEEE 802.3 (Ethernet):

| Bit lógico | 1ª metade (50 ms) | 2ª metade (50 ms) | Transição no meio do bit |
|:---:|:---:|:---:|:---:|
| `0` | LED **aceso** (ALTO) | LED **apagado** (BAIXO) | Alto → Baixo |
| `1` | LED **apagado** (BAIXO) | LED **aceso** (ALTO) | Baixo → Alto |

Esta é a mesma regra de decodificação descrita na especificação do projeto:
> "Se recebeu 1 e depois 0 → decodifica como `0`; se recebeu 0 e depois 1 → decodifica como `1`."

---

## 3. Por que o Baud Rate do Manchester foi reduzido para 10 bps?

### 3.1 O problema do LDR

O LDR (Light Dependent Resistor) possui uma característica elétrica chamada **inércia de resposta**: quando a luz diminui (transição ALTO→BAIXO), o componente demora para liberar os portadores de carga fotogerados, levando dezenas de milissegundos para cair de tensão. Já a subida (BAIXO→ALTO, quando a luz aumenta) é mais rápida.

Nos testes com NRZ-L a 20 bps (50 ms por bit), o LDR já opera no limite dessa inércia. Cada bit tem 50 ms para estabilizar, o que é suficiente para uma única transição por bit em NRZ-L.

### 3.2 O problema específico do Manchester

A codificação Manchester é **self-clocking**: ela exige uma transição **obrigatória no meio de cada bit**, independentemente do valor transmitido. Isso significa que em cada período de bit sempre ocorrem **pelo menos 1 e no máximo 2 transições** (uma no meio do bit, e possivelmente outra na borda entre bits consecutivos).

Se mantivéssemos o bit de Manchester em 50 ms (mesmo período do NRZ-L), cada metade teria apenas **25 ms** para o LDR estabilizar — tempo insuficiente para a descida de tensão do LDR, corrompendo a leitura da 2ª metade do bit.

### 3.3 A solução: dobrar o período

Mantivemos o timer base em **50 ms**, mas cada bit Manchester ocupa **2 ticks de timer**:

```
Bit Manchester = [1ª metade: 50 ms] + [2ª metade: 50 ms] = 100 ms por bit → 10 bps
```

Dessa forma, o LDR tem os mesmos 50 ms de antes para cada semi-período, garantindo estabilidade na leitura. Na prática isso significa que a transmissão de Manchester é **duas vezes mais lenta** em bps, mas é mais confiável no canal com LDR.

### 3.4 Implementação no código (TX)

Na ISR do transmissor, utilizamos a variável `manc_fase` para controlar qual metade está sendo enviada. O `bit_atual` (índice no buffer) **só avança na 2ª metade**, fazendo com que a ISR utilize o mesmo bit do buffer por 2 chamadas consecutivas:

```cpp
if (manc_fase == 0) {
    digitalWrite(LED_PIN, bit_val ? LOW : HIGH); // 1ª metade
    manc_fase = 1;
    // bit_atual NÃO avança

} else {
    digitalWrite(LED_PIN, bit_val ? HIGH : LOW); // 2ª metade
    manc_fase = 0;
    bit_atual++;  // agora avança
}
```

O buffer `buffer_tx` armazena os bytes em formato bruto — a codificação Manchester é aplicada **em tempo real pela ISR**, sem pré-expansão do buffer. Isso mantém o consumo de memória idêntico ao NRZ-L.

### 3.5 Implementação no código (RX)

No receptor, o timer permanece em 50 ms. Quando o cabeçalho indica Manchester (`codificacao == COD_MANC`), a rotina de leitura passa a consumir **2 amostras por bit** usando a variável `manc_half`:

```cpp
if (manc_half == 0) {
    manc_first_val = amostra;   // armazena 1ª metade
    manc_half = 1;

} else {
    // decodifica o par
    if      (manc_first_val == 0 && amostra == 1)  bit_dec = 1;
    else if (manc_first_val == 1 && amostra == 0)  bit_dec = 0;
    else    bit_dec = manc_first_val; // fallback para símbolo inválido
    manc_half = 0;
    // monta o byte normalmente...
}
```

---

## 4. Imunidade a Erros de DC e Detecção de Transições

### 4.1 Sem componente DC

O Manchester elimina qualquer componente DC contínua do sinal. Isso ocorre porque cada bit, independentemente do seu valor lógico, sempre termina com uma **transição obrigatória no meio do período**: a média de tempo em que o LED está aceso é sempre 50%, não importa quais bits foram enviados. 

No contexto do LDR, isso significa que o sensor nunca fica preso em um estado estável por muitos bits consecutivos (problema real do NRZ-L quando a mensagem tem muitos `0` ou `1` seguidos), o que poderia fazer o limiar de decisão derivar ao longo da transmissão.

### 4.2 Auto-sincronismo (self-clocking)

A presença garantida de transições facilita a sincronização do receptor. No preâmbulo NRZ-L `10101010` usamos transições para "acordar" o LDR e calibrar o timer. No payload Manchester, as transições continuam presentes em todos os bits, mantendo o receptor naturalmente sincronizado mesmo sem o preâmbulo.

### 4.3 Detecção passiva de erros (símbolo inválido)

Uma vantagem adicional: se o receptor lê um par `(0,0)` ou `(1,1)` onde esperava Manchester (i.e., sem transição no meio do bit), sabe imediatamente que aquele símbolo é inválido. O código trata isso com um fallback conservador (usa o valor da 1ª metade) e o **CRC-16-CCITT** captura o erro ao final do frame, garantindo que dados corrompidos não sejam aceitos silenciosamente.

### 4.4 Manchester é melhor ou pior que NRZ-L neste canal?

**Pontos positivos do Manchester no LDR:**
- Ausência de DC garante que o LDR não derive ao longo de mensagens longas.
- Transições frequentes "exercitam" o LDR, mantendo-o responsivo.
- Detecção implícita de símbolos inválidos.

**Pontos negativos:**
- Requer o dobro do tempo por bit (10 bps vs 20 bps), tornando a transmissão mais lenta.
- Com LDR lento, as bordas entre a 1ª e 2ª metade do mesmo bit (ex.: transição no meio de `0`: Alto→Baixo) são mais críticas que as bordas NRZ-L, pois o intervalo de estabilização é o mesmo (50 ms) mas a informação depende de ambas as metades.

**Conclusão:** Para mensagens longas e ambientes com variação lenta de luz ambiente, Manchester é mais robusto que NRZ-L. Para transmissões curtas e rápidas, o NRZ-L é preferível.

---

## 5. Casos de Teste Realizados

### Teste 1 — Mensagem curta
- **Entrada TX:** `"Ola"` (3 bytes)
- **Resultado RX:** Mensagem exibida corretamente, `CRC: OK`
- **Observação:** Transições visíveis no Serial Plotter com período de ~100 ms por bit no payload.

### Teste 2 — Mensagem com todos os bits iguais
- **Entrada TX:** `"AAAA"` (byte `0x41` = `01000001` × 4)
- **Resultado RX:** Mensagem exibida corretamente, `CRC: OK`
- **Observação:** Demonstra a imunidade ao DC — mesmo com muitos `0` consecutivos no meio do byte, o Manchester garante transições.

### Teste 3 — Mensagem longa (limite)
- **Entrada TX:** 64 caracteres
- **Resultado RX:** Mensagem exibida corretamente, `CRC: OK`
- **Tempo total de transmissão estimado:** Pre(4×8×50ms) + SFD(8×50ms) + Header(8×50ms) + Payload+CRC(66×8×100ms) = 160ms + 40ms + 40ms + 52800ms ≈ **53 segundos**