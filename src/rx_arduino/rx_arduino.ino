#include <TimerOne.h>
#include "crc16.h"

#define LDR_PIN A0
#define BIT_TIME_US 50000 // 50ms = 20 bps

// Tipos de Codificação 
#define COD_NRZL 0
#define COD_NRZI 1
#define COD_MANC 2

// Preambulo (4 bytes) + SFD (1 byte) + Header (1 byte) = 6 bytes = 48 bits
// sempre decodificados em NRZ-L, independente da codificação escolhida.
#define BITS_FIXOS_NRZL 48

// Limiar de decisão do LDR
uint16_t limiar_adc = 0;

// Variáveis de Interrupção
volatile bool flag_ler_adc = false;

// Máquina de estados do receptor
enum EstadoRX {
  ESPERA_SYNC,
  LENDO_PREAMBULO,
  LENDO_SFD,
  LENDO_HEADER,
  LENDO_PAYLOAD,
  LENDO_CRC
};

EstadoRX estado = ESPERA_SYNC;

// Buffers e Contadores
uint8_t buffer_rx[75];
uint16_t bit_count = 0;
uint16_t byte_count = 0;
uint8_t bit_atual_val = 0;
uint8_t byte_montado = 0;

// Propriedades do Frame
uint8_t tamanho_payload = 0;
uint8_t codificacao = 0; // 0=NRZL, 1=NRZI, 2=MANC

// Estado necessário para decodificar NRZ-I
volatile uint8_t nivel_fisico_anterior = 0;   // último nível (0/1) lido do LDR
volatile uint32_t bits_totais_recebidos = 0;  // contador de bits do frame atual

void setup() {
  Serial.begin(9600);
  pinMode(LDR_PIN, INPUT);
  
  Serial.println("--- RX INICIANDO ---");
  calibrar_ldr();
  
  // Timer só é ativado de verdade após o sync, mas deixamos configurado
  Timer1.initialize(BIT_TIME_US);
  Timer1.attachInterrupt(isr_marca_leitura);
  Timer1.stop();
  
  Serial.println("Aguardando transmissao...");
}

void calibrar_ldr() {
  Serial.println("Calibrando LDR...");
  Serial.println("1. Deixe o ambiente como repouso (LED TX apagado) por 3 seg...");
  delay(3000);
  long soma_min = 0;
  for(int i=0; i<100; i++) { soma_min += analogRead(LDR_PIN); delay(10); }
  uint16_t v_min = soma_min / 100;
  
  Serial.println("2. Ligue o LED TX apontado para o LDR fixamente por 3 seg...");
  delay(3000);
  long soma_max = 0;
  for(int i=0; i<100; i++) { soma_max += analogRead(LDR_PIN); delay(10); }
  uint16_t v_max = soma_max / 100;
  
  limiar_adc = (v_min + v_max) / 2;
  Serial.print("V_min: "); Serial.print(v_min);
  Serial.print(" | V_max: "); Serial.print(v_max);
  Serial.print(" | Limiar: "); Serial.println(limiar_adc);
}

// A ISR deve ser o mais curta possível. Não use analogRead aqui.
void isr_marca_leitura() {
  flag_ler_adc = true;
}

void loop() {
  // --- ETAPA DE SINCRONISMO (AUTO-BAUD BÁSICO) ---
  if (estado == ESPERA_SYNC) {
    if (analogRead(LDR_PIN) > limiar_adc) {
      // Começou a receber o preâmbulo (Transição para HIGH).
      // Na versão final, faremos o cálculo da média com micros() aqui.
      // Por enquanto, usaremos a taxa fixa com o atraso de meio bit para centralizar.
      delayMicroseconds(BIT_TIME_US / 2); // Espera ir para o meio do bit
      Timer1.restart(); // Inicia o timer
      estado = LENDO_PREAMBULO;
      bit_count = 0;
      byte_montado = 0;
      bits_totais_recebidos = 0;
      nivel_fisico_anterior = 0;
      Serial.println("Detectado Preamble! Sincronizando...");
    }
  }

  // --- ETAPA DE LEITURA (ACIONADA PELO TIMER) ---
  if (flag_ler_adc) {
    flag_ler_adc = false; // Abaixa a bandeira
    
    // Processamento da amostra (Media móvel rápida para evitar ruído)
    long soma = 0;
    for(int i=0; i<3; i++) soma += analogRead(LDR_PIN);
    uint16_t leitura = soma / 3;
    uint8_t nivel_fisico = (leitura > limiar_adc) ? 1 : 0;

    if (bits_totais_recebidos < BITS_FIXOS_NRZL || codificacao == COD_NRZL) {
      // Preambulo + SFD + Header sempre NRZ-L. Payload/CRC tambem, se essa
      // foi a codificacao escolhida (comportamento original, preservado).
      bit_atual_val = nivel_fisico;
    } else if (codificacao == COD_NRZI) {
      // NRZ-I: mudou de nivel em relacao a amostra anterior -> bit 1
      //         manteve o mesmo nivel -> bit 0
      bit_atual_val = (nivel_fisico != nivel_fisico_anterior) ? 1 : 0;
    } else {
      // COD_MANC (Manchester) fica a cargo do Kroda.
      bit_atual_val = nivel_fisico;
    }

    nivel_fisico_anterior = nivel_fisico;
    bits_totais_recebidos++;

    // Monta o byte (MSB primeiro)
    byte_montado = (byte_montado << 1) | bit_atual_val;
    bit_count++;
    
    if (bit_count == 8) {
      // Fechamos 1 byte
      processa_byte_recebido(byte_montado);
      bit_count = 0;
      byte_montado = 0;
    }
  }
}

void processa_byte_recebido(uint8_t byte_rx) {
  switch(estado) {
    case LENDO_PREAMBULO:
      if (byte_rx == 0xAA) {
        // Continua lendo preambulo
      } else if (byte_rx == 0xF0) {
        estado = LENDO_HEADER;
      } else {
        // Ruído, aborta
        reseta_rx();
      }
      break;
      
    case LENDO_HEADER:
      codificacao = (byte_rx >> 6) & 0x03;
      tamanho_payload = (byte_rx & 0x3F) + 1; // +1 porque foi enviado como tamanho-1
      buffer_rx[0] = byte_rx; // Guarda pro CRC
      byte_count = 1;
      estado = LENDO_PAYLOAD;
      break;
      
    case LENDO_PAYLOAD:
      buffer_rx[byte_count++] = byte_rx;
      if (byte_count == tamanho_payload + 1) { // header(1) + payload
        estado = LENDO_CRC;
      }
      break;
      
    case LENDO_CRC:
      buffer_rx[byte_count++] = byte_rx;
      if (byte_count == tamanho_payload + 3) { // header(1) + payload + crc(2)
        finaliza_pacote();
      }
      break;
  }
}

void finaliza_pacote() {
  Timer1.stop(); // Para de ler
  
  // O buffer_rx tem: [Header] [Payload...] [CRC High] [CRC Low]
  uint16_t crc_recebido = (buffer_rx[byte_count-2] << 8) | buffer_rx[byte_count-1];
  
  // Recalcula CRC sobre Header + Payload
  uint16_t crc_calculado = calcular_crc16(buffer_rx, byte_count - 2);
  
  Serial.println("--- PACOTE RECEBIDO ---");
  if (crc_recebido == crc_calculado) {
    Serial.println("CRC: OK!");
    Serial.print("Mensagem: ");
    
    // Imprime payload
    for(int i=1; i <= tamanho_payload; i++) {
      Serial.print((char)buffer_rx[i]);
    }
    Serial.println();
  } else {
    Serial.println("CRC: ERRO!");
    Serial.print("Recebido: "); Serial.print(crc_recebido, HEX);
    Serial.print(" | Calculado: "); Serial.println(crc_calculado, HEX);
  }
  Serial.println("-----------------------");
  
  reseta_rx();
}

void reseta_rx() {
  estado = ESPERA_SYNC;
  Timer1.stop();
  bits_totais_recebidos = 0;
  nivel_fisico_anterior = 0;
}
