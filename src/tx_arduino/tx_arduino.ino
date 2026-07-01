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
volatile uint8_t  cod_atual_tx      = COD_NRZL; // Padrão

// payload_start_bit: índice (em bits) a partir do qual aplica a codificação.
// Sempre será 48 = (Preâmbulo 4B + SFD 1B + Header 1B) × 8.
#define BITS_FIXOS_NRZL 48
volatile uint16_t payload_start_bit = 48;

// ── Controle de codificação NRZ-I ────────────────────────────────────────────
// Último nível físico (HIGH/LOW) que foi escrito no LED. Necessário para o NRZ-I
// saber se deve inverter ou manter o estado no próximo bit.
volatile bool nivel_linha_anterior = LOW;

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
  Serial.println(F(" TX PRONTO — Suporte NRZ-L, NRZ-I e Manchester"));
  Serial.println(F("============================================="));
  Serial.println(F("Digite a mensagem (max 64 chars) e pressione Enter."));
  Serial.println(F("Prefixo: 'I:' = NRZ-I, 'M:' = Manchester, 'L:' = NRZ-L (padrao = NRZ-L)."));
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  if (Serial.available() > 0 && !transmitindo) {
    int n = Serial.readBytesUntil('\n', mensagem, 64);
    mensagem[n] = '\0';

    if (n > 0) {
      uint8_t codificacao_escolhida = COD_NRZL; // Padrão
      char* msg_ptr = mensagem;
      int tamanho_msg = n;

      if (n > 2 && mensagem[1] == ':') {
        if (mensagem[0] == 'I' || mensagem[0] == 'i') {
          codificacao_escolhida = COD_NRZI;
        } else if (mensagem[0] == 'M' || mensagem[0] == 'm') {
          codificacao_escolhida = COD_MANC;
        } else if (mensagem[0] == 'L' || mensagem[0] == 'l') {
          codificacao_escolhida = COD_NRZL;
        }
        msg_ptr = mensagem + 2;
        tamanho_msg = n - 2;
      }

      preparar_frame(msg_ptr, tamanho_msg, codificacao_escolhida);
      transmitindo = true;

      Serial.print(F("[TX] Enviando ("));
      if (codificacao_escolhida == COD_NRZI) Serial.print(F("NRZ-I"));
      else if (codificacao_escolhida == COD_MANC) Serial.print(F("Manchester"));
      else Serial.print(F("NRZ-L"));
      Serial.print(F("): \""));
      Serial.print(msg_ptr);
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
    uint8_t bit_idx = 7 - (bit_atual % 8);
    bool    bit_val = (buffer_tx[byte_idx] >> bit_idx) & 0x01;

    bool decodificando_payload = (bit_atual >= payload_start_bit);
    bool modo_manchester = (cod_atual_tx == COD_MANC) && decodificando_payload;
    bool modo_nrzi       = (cod_atual_tx == COD_NRZI) && decodificando_payload;

    if (modo_manchester) {
      if (manc_fase == 0) {
        // === 1ª METADE ===
        digitalWrite(LED_PIN, bit_val ? LOW : HIGH);
        manc_fase = 1;
      } else {
        // === 2ª METADE ===
        digitalWrite(LED_PIN, bit_val ? HIGH : LOW);
        manc_fase = 0;
        bit_atual++;
      }
    } else {
      bool nivel_saida;
      if (modo_nrzi) {
        // NRZ-I: bit 1 INVERTE o nivel anterior da linha, bit 0 MANTEM o nivel.
        nivel_saida = bit_val ? !nivel_linha_anterior : nivel_linha_anterior;
      } else {
        // NRZ-L: bit 1 = HIGH, bit 0 = LOW
        nivel_saida = bit_val;
      }

      digitalWrite(LED_PIN, nivel_saida ? HIGH : LOW);
      nivel_linha_anterior = nivel_saida;
      bit_atual++;
    }
  } else {
    // ── Fim da transmissão ────────────────────────────────────────────────
    transmitindo = false;
    bit_atual    = 0;
    manc_fase    = 0;
    nivel_linha_anterior = LOW;
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
  nivel_linha_anterior = LOW;

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
