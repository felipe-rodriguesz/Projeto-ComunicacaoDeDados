// =============================================================================
// rx_arduino.ino — Receptor com suporte a NRZ-L e Manchester
// Projeto: Comunicação Óptica por LED/LDR — UTFPR
//
// Módulo Manchester implementado por: Kroda
// Integração e arquitetura geral: Felipe
//
// DECODIFICAÇÃO MANCHESTER:
//   Cada bit lógico chega em DUAS amostras consecutivas de 50 ms:
//     (1ª metade=ALTO, 2ª metade=BAIXO) → bit '0'   (Alto→Baixo)
//     (1ª metade=BAIXO, 2ª metade=ALTO) → bit '1'   (Baixo→Alto)
//
// O timer permanece em 50 ms para todos os estados. A diferença é que no
// payload Manchester o receptor consome 2 ticks por bit em vez de 1.
// =============================================================================

#include <TimerOne.h>
#include "crc16.h"

// ── Pinos e temporização ──────────────────────────────────────────────────────
#define LDR_PIN     A0
#define BIT_TIME_US 50000UL   // 50 ms — base comum a NRZ-L e meia-fatia Manchester

// ── Tipos de codificação ──────────────────────────────────────────────────────
#define COD_NRZL 0
#define COD_NRZI 1
#define COD_MANC 2

// Preambulo (4 bytes) + SFD (1 byte) + Header (1 byte) = 6 bytes = 48 bits
// sempre decodificados em NRZ-L, independente da codificação escolhida.
#define BITS_FIXOS_NRZL 48

// ── Limiar de decisão do LDR (calibrado no setup) ────────────────────────────
uint16_t limiar_adc = 0;

// ── Flag de interrupção (ISR apenas levanta a flag) ──────────────────────────
volatile bool flag_ler_adc = false;

// ── Máquina de Estados Finita (FSM) do receptor ──────────────────────────────
enum EstadoRX {
  ESPERA_SYNC,
  LENDO_PREAMBULO,
  LENDO_SFD,       // reservado — SFD é detectado dentro de LENDO_PREAMBULO
  LENDO_HEADER,
  LENDO_PAYLOAD,
  LENDO_CRC
};
EstadoRX estado = ESPERA_SYNC;

// ── Buffers e contadores ──────────────────────────────────────────────────────
uint8_t  buffer_rx[75];
uint16_t bit_count    = 0;
uint16_t byte_count   = 0;
uint8_t  byte_montado = 0;

// ── Propriedades do frame detectado ──────────────────────────────────────────
uint8_t tamanho_payload = 0;
uint8_t codificacao     = 0;

// ── Estado necessário para decodificar NRZ-I ─────────────────────────────────
volatile uint8_t nivel_fisico_anterior = 0;   // último nível (0/1) lido do LDR
volatile uint32_t bits_totais_recebidos = 0;  // contador de bits do frame atual

// ── Controle de decodificação Manchester ─────────────────────────────────────
// manc_half:      0 = aguardando 1ª metade do bit atual
//                 1 = aguardando 2ª metade do bit atual
// manc_first_val: valor (0 ou 1) amostrado na 1ª metade
uint8_t manc_half      = 0;
uint8_t manc_first_val = 0;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(9600);
  pinMode(LDR_PIN, INPUT);

  Serial.println(F("============================================="));
  Serial.println(F(" RX PRONTO — Suporte NRZ-L + Manchester"));
  Serial.println(F("============================================="));

  calibrar_ldr();

  Timer1.initialize(BIT_TIME_US);
  Timer1.attachInterrupt(isr_marca_leitura);
  Timer1.stop();   // Timer só ativa após sync

  Serial.println(F("Aguardando transmissao..."));
}

// =============================================================================
// CALIBRAÇÃO DO LDR
// =============================================================================
void calibrar_ldr() {
  Serial.println(F("--- Calibracao do LDR ---"));

  Serial.println(F("Passo 1: Mantenha o LED TX APAGADO por 3 segundos..."));
  delay(3000);
  long soma = 0;
  for (int i = 0; i < 100; i++) { soma += analogRead(LDR_PIN); delay(10); }
  uint16_t v_min = soma / 100;

  Serial.println(F("Passo 2: APONTE o LED TX para o LDR e mantenha por 3 segundos..."));
  delay(3000);
  soma = 0;
  for (int i = 0; i < 100; i++) { soma += analogRead(LDR_PIN); delay(10); }
  uint16_t v_max = soma / 100;

  limiar_adc = (v_min + v_max) / 2;
  Serial.print(F("V_min=")); Serial.print(v_min);
  Serial.print(F(" | V_max=")); Serial.print(v_max);
  Serial.print(F(" | Limiar=")); Serial.println(limiar_adc);
  Serial.println(F("-------------------------"));
}

// =============================================================================
// ISR — Não faz nada além de levantar a flag (mínimo overhead)
// =============================================================================
void isr_marca_leitura() {
  flag_ler_adc = true;
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {

  // ── Sincronismo: detecta borda de subida do preâmbulo ────────────────────
  if (estado == ESPERA_SYNC) {
    if (analogRead(LDR_PIN) > limiar_adc) {
      // 1. Anota o tempo do primeiro pulso ALTO (1º bit do preâmbulo)
      uint32_t t0 = micros();

      // 2. Aguarda a luz APAGAR (início do 2º bit '0'), com timeout de 200ms
      while (analogRead(LDR_PIN) > limiar_adc && (micros() - t0) < 200000UL) {}

      // 3. Calcula o tempo exato do 1º bit
      uint32_t t1 = micros();
      uint32_t bit_time_real = t1 - t0;

      // Valida se o pulso durou um tempo razoável (entre 10ms e 200ms)
      if (bit_time_real > 10000UL && bit_time_real < 200000UL) {
        
        // Estamos na borda de descida (início do 2º bit).
        // Dá um delay de MEIO bit_time para centralizar a amostragem no meio do bit.
        delayMicroseconds(bit_time_real / 2);

        // Inicializa o Timer1 com o baud rate dinâmico descoberto.
        // Como chamamos restart() no meio do 2º bit, a primeira interrupção (ISR)
        // cairá exatamente no MEIO do 3º bit.
        Timer1.initialize(bit_time_real);
        Timer1.restart();

        // Já "lemos" os 2 primeiros bits (obrigatoriamente 1 e 0 = 0b10 = 2).
        byte_montado = 2;         
        bit_count = 2;            
        bits_totais_recebidos = 2; 
        nivel_fisico_anterior = 0; // 2º bit era 0
        
        estado = LENDO_PREAMBULO;
        manc_half = 0;

        Serial.print(F("[RX] Auto-baud cravado! Bit_time adaptado: "));
        Serial.print(bit_time_real);
        Serial.println(F(" us"));
      }
    }
  }

  // ── Leitura acionada pelo Timer (a cada 50 ms) ────────────────────────────
  if (flag_ler_adc) {
    flag_ler_adc = false;

    // Média de 3 leituras consecutivas para suavizar ruído óptico
    long soma = 0;
    for (int i = 0; i < 3; i++) soma += analogRead(LDR_PIN);
    uint8_t nivel_fisico = ((soma / 3) > limiar_adc) ? 1 : 0;

    bool decodificando_payload = (estado == LENDO_PAYLOAD || estado == LENDO_CRC);
    bool modo_manchester = (codificacao == COD_MANC) && decodificando_payload;
    bool modo_nrzi       = (codificacao == COD_NRZI) && decodificando_payload;

    if (modo_manchester) {
      // ══ MANCHESTER: 2 ticks = 1 bit ════════════════════════════════════
      if (manc_half == 0) {
        manc_first_val = nivel_fisico;
        manc_half      = 1;
      } else {
        uint8_t bit_dec;
        if      (manc_first_val == 0 && nivel_fisico == 1)  bit_dec = 1;
        else if (manc_first_val == 1 && nivel_fisico == 0)  bit_dec = 0;
        else    bit_dec = manc_first_val; // Fallback no caso de ruído

        manc_half    = 0;
        byte_montado = (byte_montado << 1) | bit_dec;
        if (++bit_count == 8) {
          processa_byte_recebido(byte_montado);
          bit_count    = 0;
          byte_montado = 0;
        }
      }
    } else {
      // ══ NRZ-L ou NRZ-I: 1 tick = 1 bit ══════════════════════════════════
      uint8_t bit_atual_val = 0;
      
      if (modo_nrzi) {
        bit_atual_val = (nivel_fisico != nivel_fisico_anterior) ? 1 : 0;
      } else {
        bit_atual_val = nivel_fisico;
      }

      byte_montado = (byte_montado << 1) | bit_atual_val;
      if (++bit_count == 8) {
        processa_byte_recebido(byte_montado);
        bit_count    = 0;
        byte_montado = 0;
      }
    }

    // Armazena estado para a próxima iteração
    nivel_fisico_anterior = nivel_fisico;
    bits_totais_recebidos++;
  }
}

// =============================================================================
// processa_byte_recebido — FSM que monta o frame byte a byte
// =============================================================================
void processa_byte_recebido(uint8_t byte_rx) {
  switch (estado) {

    // ── Preâmbulo: espera 0xAA repetido, depois 0xF0 (SFD) ───────────────
    case LENDO_PREAMBULO:
      if (byte_rx == 0xAA) {
        // Byte de preâmbulo válido — permanece no estado aguardando mais
      } else if (byte_rx == 0xF0) {
        // SFD detectado: próximo byte é o cabeçalho
        estado    = LENDO_HEADER;
        manc_half = 0;
        Serial.println(F("[RX] SFD detectado. Lendo cabecalho..."));
      } else {
        // Byte inesperado = ruído no canal → aborta recepção
        Serial.print(F("[RX] ERRO preambulo. Byte=0x"));
        Serial.println(byte_rx, HEX);
        reseta_rx();
      }
      break;

    // ── Cabeçalho: extrai codificação e tamanho do payload ───────────────
    case LENDO_HEADER:
      codificacao     = (byte_rx >> 6) & 0x03;
      tamanho_payload = (byte_rx & 0x3F) + 1;  // recupera tamanho real (+1)
      buffer_rx[0]    = byte_rx;
      byte_count      = 1;
      manc_half       = 0;   // garante alinhamento de fase para o payload
      estado          = LENDO_PAYLOAD;

      Serial.print(F("[RX] Cabecalho: cod="));
      if      (codificacao == COD_NRZL) Serial.print(F("NRZ-L"));
      else if (codificacao == COD_NRZI) Serial.print(F("NRZ-I"));
      else                               Serial.print(F("Manchester"));
      Serial.print(F(" | payload="));
      Serial.print(tamanho_payload);
      Serial.println(F(" bytes"));
      break;

    // ── Payload: acumula bytes até completar tamanho_payload ─────────────
    case LENDO_PAYLOAD:
      buffer_rx[byte_count++] = byte_rx;
      if (byte_count == (uint16_t)tamanho_payload + 1) {
        // header(1) + payload completo
        manc_half = 0;   // garante alinhamento de fase para o CRC
        estado    = LENDO_CRC;
        Serial.println(F("[RX] Payload completo. Aguardando CRC..."));
      }
      break;

    // ── CRC: acumula 2 bytes e finaliza o pacote ──────────────────────────
    case LENDO_CRC:
      buffer_rx[byte_count++] = byte_rx;
      if (byte_count == (uint16_t)tamanho_payload + 3) {
        // header(1) + payload + CRC(2)
        finaliza_pacote();
      }
      break;

    default:
      break;
  }
}

// =============================================================================
// finaliza_pacote — Valida CRC e exibe resultado
// =============================================================================
void finaliza_pacote() {
  Timer1.stop();

  // Extrai CRC recebido (últimos 2 bytes do buffer)
  uint16_t crc_recv = ((uint16_t)buffer_rx[byte_count - 2] << 8)
                    |             buffer_rx[byte_count - 1];

  // Recalcula CRC sobre [Cabeçalho + Payload] (buffer_rx[0] até byte_count-3)
  uint16_t crc_calc = calcular_crc16(buffer_rx, byte_count - 2);

  Serial.println(F("=============================="));
  Serial.println(F("     PACOTE RECEBIDO"));
  Serial.println(F("=============================="));

  if (crc_recv == crc_calc) {
    Serial.println(F("CRC: OK [integro]"));
    Serial.print(F("Mensagem: \""));
    for (int i = 1; i <= tamanho_payload; i++) {
      Serial.print((char)buffer_rx[i]);
    }
    Serial.println(F("\""));
  } else {
    Serial.println(F("CRC: FALHOU [corrompido]"));
    Serial.print(F("  Recebido : 0x")); Serial.println(crc_recv, HEX);
    Serial.print(F("  Calculado: 0x")); Serial.println(crc_calc, HEX);
  }

  Serial.println(F("=============================="));
  reseta_rx();
}

// =============================================================================
// reseta_rx — Retorna a FSM ao estado inicial
// =============================================================================
void reseta_rx() {
  estado       = ESPERA_SYNC;
  manc_half    = 0;
  bit_count    = 0;
  byte_montado = 0;
  byte_count   = 0;
  Timer1.stop();
  bits_totais_recebidos = 0;
  nivel_fisico_anterior = 0;
  Serial.println(F("[RX] Aguardando proxima transmissao...\n"));
}
