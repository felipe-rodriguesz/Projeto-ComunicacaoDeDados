# Comunicação de Dados Óptica com Arduino (LED + LDR)

Projeto acadêmico da UTFPR que implementa comunicação simplex por luz visível entre dois Arduino Uno: um transmissor (TX) com LED e um receptor (RX) com LDR. O sistema combina enquadramento serial próprio, três modos de codificação, estimativa do tempo de bit, redundância no payload e verificação CRC-8.

## Estado

O código na branch principal reúne as implementações de NRZ-L, NRZ-I e Manchester, auto-baud baseado em `millis()`, redundância por byte e CRC-8 implementado nos sketches, sem bibliotecas de terceiros.

## Validação em hardware

O transmissor e o receptor foram testados com o hardware físico. O enlace funcionou na bancada usando o sistema real de LED/LDR e os dois Arduinos. Esse resultado registra a validação realizada; não representa uma garantia de funcionamento em toda montagem ou condição de operação.

## Hardware

Montagem utilizada pelo projeto:

- **TX:** Arduino Uno e LED de alto brilho. Ânodo no pino digital 8; cátodo ligado em série a um resistor de 330 Ω e ao GND.
- **RX:** Arduino Uno, LDR e resistor de 3,3 kΩ. LDR ligado ao 5 V; nó entre LDR e resistor ligado à entrada analógica A0; outra extremidade do resistor ligada ao GND.

O RX classifica a leitura analógica usando o limiar fixo `400` definido no código. O comportamento observado depende do conjunto óptico e da montagem.

## Protocolo implementado

Cada transmissão contém, nesta ordem:

1. **Pulso de calibração:** o TX mantém o LED ligado durante cinco tempos de bit. O RX mede a duração do pulso com `millis()` e divide o valor por cinco para estimar o tempo de bit. A resolução é limitada pela medição em milissegundos, pelo polling e pelas transições observadas no LDR.
2. **Tamanho:** um byte com o comprimento da mensagem, de até 64 caracteres.
3. **Payload com redundância:** para cada byte `d`, são enviados `d`, `~d` e `d ^ 0xAA`.
4. **CRC-8:** um byte calculado sobre os bytes da mensagem original (sem incluir tamanho ou redundância).

O FEC do receptor desfaz as transformações das três representações e vota bit a bit. Para cada posição, a maioria tolera uma representação incorreta entre as três. Isso não equivale a uma garantia geral de corrigir “até três bits errados”: se duas ou três representações da mesma posição forem lidas incorretamente, o resultado dessa posição pode estar errado. O CRC verifica a mensagem reconstruída e informa divergência; não corrige os dados. O FEC protege somente os bytes do payload, não o tamanho, o CRC nem os bits de enquadramento.

O CRC-8 usa o polinômio `0x07`, valor inicial `0x00`, processamento MSB primeiro e sem reflexão ou XOR final explícitos. TX e RX implementam o mesmo algoritmo.

## Codificações

TX e RX precisam estar configurados no mesmo modo. O modo é escolhido manualmente no Serial do receptor e no transmissor; o protocolo não negocia essa configuração.

- **NRZ-L (modo 1):** nível alto representa `1`; nível baixo representa `0`.
- **NRZ-I (modo 2):** `1` inverte o nível anterior; `0` mantém o nível.
- **Manchester (modo 3):** cada bit é transmitido em duas metades, com a convenção implementada no TX. O RX amostra o nível na segunda metade do bit; não verifica explicitamente a transição central como faria um decodificador Manchester genérico.

Os bits de start e stop são níveis convencionais e não usam a codificação Manchester.

## Parâmetros e execução

- O TX solicita o tempo de bit em milissegundos, o modo (`1`, `2` ou `3`) e a mensagem. O código limita a mensagem a 64 caracteres.
- O tempo de bit pode ser informado pelo usuário, mas o código não valida uma faixa mínima ou máxima. A estimativa do RX tem resolução limitada, e a operação depende do tempo de resposta do LDR, do polling e da temporização do enlace.
- Abra os sketches em uma IDE Arduino e carregue `src/tx_arduino/tx_arduino.ino` no TX e `src/rx_arduino/rx_arduino.ino` no RX.
- Configure os dois Monitores Seriais em 115200 baud. No RX, selecione o modo de codificação; no TX, informe o tempo de bit, selecione o mesmo modo e digite a mensagem.

## Estrutura do projeto

```text
├── README.md
├── .gitignore
├── docs/
│   ├── ARCHITECTURE.md
│   ├── CONTRIBUTING.md
│   └── kroda_relatorio.md
└── src/
    ├── rx_arduino/rx_arduino.ino
    └── tx_arduino/tx_arduino.ino
```

Consulte [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) para detalhes das decisões de implementação, [docs/kroda_relatorio.md](docs/kroda_relatorio.md) para o registro técnico/histórico do módulo Manchester e [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) para o fluxo de contribuição da equipe.
