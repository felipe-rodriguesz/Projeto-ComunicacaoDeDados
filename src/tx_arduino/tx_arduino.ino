// =============================================================================
// tx_arduino.ino — Transmissor com suporte a NRZ-L e Manchester
// Projeto: Comunicação Óptica por LED/LDR — UTFPR
//
// Módulo Manchester implementado por: Kroda
// Integração e arquitetura geral: Felipe
//
// CONVENÇÃO MANCHESTER ADOTADA (IEEE 802.3):
//   bit '0' → LED ACENDE (1ª metade) depois APAGA  (2ª metade)  ← Alto→Baixo
//   bit '1' → LED APAGA  (1ª metade) depois ACENDE (2ª metade)  ← Baixo→Alto
//
// TAXAS:
//   Preâmbulo / SFD / Cabeçalho → NRZ-L a 20 bps  (1 tick × 50 ms = 1 bit)
//   Payload + CRC Manchester    → 10 bps           (2 ticks × 50 ms = 1 bit)
//
// POR QUE 10 bps NO MANCHESTER?
//   O timer base permanece em 50 ms. No Manchester cada bit lógico ocupa DUAS
//   fatias de 50 ms (primeira metade + segunda metade), resultando em 100 ms
//   por bit → 10 bps.  A lentidão extra é necessária para o LDR ter tempo de
//   reagir à transição obrigatória no meio de cada bit.
// =============================================================================

#include <TimerOne.h>
#include "crc16.h"

// ── Pinos e temporização ──────────────────────────────────────────────────────
#define LED_PIN      8
#define BIT_TIME_US  50000UL   // 50 ms — base comum a NRZ-L e meia-fatia Manchester

// ── Tipos de codificação (espelham os 2 bits do cabeçalho) ───────────────────
#define COD_NRZL  0   // 00 — NRZ-L
#define COD_NRZI  1   // 01 — NRZ-I  (módulo do Elder)
#define COD_MANC  2   // 10 — Manchester

// ── Estrutura do Frame ────────────────────────────────────────────────────────
static const uint8_t PREAMBLE[4] = { 0xAA, 0xAA, 0xAA, 0xAA }; // 10101010 ×4
static const uint8_t SFD         =   0xF0;                       // 11110000

// ── Buffers ───────────────────────────────────────────────────────────────────
char    mensagem[65];
uint8_t buffer_tx[75]; // Pre(4) + SFD(1) + Header(1) + Payload(≤64) + CRC(2)

// ── Variáveis compartilhadas com a ISR (volatile obrigatório) ────────────────
volatile uint16_t total_bits        = 0;
volatile uint16_t bit_atual         = 0;
volatile bool     transmitindo      = false;
volatile uint8_t  cod_atual_tx      = COD_MANC;
// payload_start_bit: índice (em bits) a partir do qual aplica Manchester.
// Sempre será 48 = (Preâmbulo 4B + SFD 1B + Header 1B) × 8.
volatile uint16_t payload_start_bit = 48;

// ── Controle de fase Manchester ───────────────────────────────────────────────
// 0 = enviando 1ª metade do bit atual
// 1 = enviando 2ª metade do bit atual (avança bit_atual ao final)
volatile uint8_t manc_fase = 0;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Timer1.initialize(BIT_TIME_US);
  Timer1.attachInterrupt(isr_transmite_bit);

  Serial.println(F("============================================="));
  Serial.println(F(" TX PRONTO — Codificacao Manchester (10 bps)"));
  Serial.println(F("============================================="));
  Serial.println(F("Digite a mensagem (max 64 chars) e pressione Enter:"));
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  if (Serial.available() > 0 && !transmitindo) {
    int n = Serial.readBytesUntil('\n', mensagem, 64);
    mensagem[n] = '\0';

    if (n > 0) {
      preparar_frame(mensagem, (uint8_t)n, COD_MANC);
      transmitindo = true;

      Serial.print(F("[TX] Enviando (Manchester): \""));
      Serial.print(mensagem);
      Serial.println(F("\""));
      Serial.print(F("[TX] Total de bits no buffer: "));
      Serial.println(total_bits);
    }
  }
}

// =============================================================================
// ISR — Acionada a cada BIT_TIME_US (50 ms)
//
// Lógica de tempo:
//   • Bits 0–47  (preâmbulo, SFD, cabeçalho): NRZ-L → 1 tick = 1 bit
//   • Bits 48–N  (payload + CRC Manchester)  : 2 ticks = 1 bit
//     tick de fase 0 → primeira metade  (não avança bit_atual)
//     tick de fase 1 → segunda metade   (avança bit_atual)
// =============================================================================
void isr_transmite_bit() {
  if (!transmitindo) return;

  if (bit_atual < total_bits) {

    // Localiza o bit lógico no buffer (MSB primeiro)
    uint16_t byte_idx = bit_atual / 8;
    uint8_t  bit_idx  = 7 - (bit_atual % 8);
    bool     bit_val  = (buffer_tx[byte_idx] >> bit_idx) & 0x01;

    // ── Manchester: payload e CRC ─────────────────────────────────────────
    if (cod_atual_tx == COD_MANC && bit_atual >= payload_start_bit) {

      if (manc_fase == 0) {
        // === 1ª METADE ===
        // bit '0' → inicia ALTO  (LED acende)
        // bit '1' → inicia BAIXO (LED apaga)
        digitalWrite(LED_PIN, bit_val ? LOW : HIGH);
        manc_fase = 1;
        // bit_atual NÃO avança — reutilizamos o mesmo bit na próxima ISR

      } else {
        // === 2ª METADE ===
        // bit '0' → termina BAIXO (LED apaga)   → transição Alto→Baixo = '0'
        // bit '1' → termina ALTO  (LED acende)  → transição Baixo→Alto = '1'
        digitalWrite(LED_PIN, bit_val ? HIGH : LOW);
        manc_fase = 0;
        bit_atual++;  // Bit completo: avança para o próximo
      }

    // ── NRZ-L: preâmbulo, SFD, cabeçalho (e payload se COD_NRZL) ─────────
    } else {
      digitalWrite(LED_PIN, bit_val ? HIGH : LOW);
      bit_atual++;
    }

  } else {
    // ── Fim da transmissão ────────────────────────────────────────────────
    transmitindo = false;
    bit_atual    = 0;
    manc_fase    = 0;
    digitalWrite(LED_PIN, LOW);
    Serial.println(F("[TX] Transmissao concluida."));
  }
}

// =============================================================================
// preparar_frame — Monta o frame completo no buffer_tx
//
// Estrutura: [Preâmbulo ×4][SFD][Cabeçalho][Payload][CRC-H][CRC-L]
// O buffer armazena os DADOS BRUTOS. A codificação Manchester é aplicada
// bit a bit pela ISR em tempo real — não há pré-expansão do buffer.
// =============================================================================
void preparar_frame(char* msg, uint8_t tamanho, uint8_t codificacao) {
  uint16_t idx = 0;

  // Inicializa controle de estado
  cod_atual_tx = codificacao;
  manc_fase    = 0;

  // 1. Preâmbulo — 4 bytes, sempre NRZ-L
  for (int i = 0; i < 4; i++) buffer_tx[idx++] = PREAMBLE[i];

  // 2. SFD — 1 byte, sempre NRZ-L
  buffer_tx[idx++] = SFD;

  // 3. Cabeçalho — 1 byte, sempre NRZ-L
  // [7:6] = tipo de codificação | [5:0] = (tamanho_real - 1)
  uint8_t header = (codificacao << 6) | ((tamanho - 1) & 0x3F);
  buffer_tx[idx++] = header;

  // Registra o bit de início do payload (sempre 48 = 6 bytes × 8 bits)
  payload_start_bit = (uint16_t)idx * 8;

  // 4. Payload bruto — codificação feita na ISR
  for (int i = 0; i < tamanho; i++) buffer_tx[idx++] = (uint8_t)msg[i];

  // 5. CRC-16-CCITT sobre [Cabeçalho + Payload]
  // buffer_tx[5] aponta para o cabeçalho; tamanho total = 1 (header) + tamanho (payload)
  uint16_t crc = calcular_crc16(&buffer_tx[5], 1 + tamanho);
  buffer_tx[idx++] = (crc >> 8) & 0xFF;  // CRC High Byte
  buffer_tx[idx++] =  crc       & 0xFF;  // CRC Low Byte

  // Total de bits do buffer (a ISR gerencia o dobramento Manchester)
  total_bits = (uint16_t)idx * 8;
  bit_atual  = 0;

  Serial.print(F("[TX] Frame montado: "));
  Serial.print(idx);
  Serial.print(F(" bytes | payload_start_bit="));
  Serial.println(payload_start_bit);
}
