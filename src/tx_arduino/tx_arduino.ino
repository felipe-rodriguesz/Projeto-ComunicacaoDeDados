#include <TimerOne.h>
#include "crc16.h"

#define LED_PIN 8
#define BIT_TIME_US 50000 // 50ms por bit = 20 bps (Baud Rate Conservador)

// Estrutura do Pacote
const uint8_t PREAMBLE[4] = {0xAA, 0xAA, 0xAA, 0xAA}; // 10101010 x4
const uint8_t SFD = 0xF0; // 11110000

// Buffers
char mensagem[65]; 
uint8_t buffer_tx[75]; // Pre(4) + SFD(1) + Header(1) + Payload(max 64) + CRC(2) = 72
volatile uint16_t total_bits = 0;
volatile uint16_t bit_atual = 0;
volatile bool transmitindo = false;

// Tipos de Codificação
#define COD_NRZL 0
#define COD_NRZI 1
#define COD_MANC 2

// Quantidade de bits fixos do frame que SEMPRE vão em NRZ-L
// Preambulo (4 bytes) + SFD (1 byte) + Header (1 byte) = 6 bytes = 48 bits
#define BITS_FIXOS_NRZL 48

// Estado da codificação selecionada para o frame atual (definido em preparar_frame)
volatile uint8_t codificacao_atual_tx = COD_NRZL;
// Último nível físico (HIGH/LOW) que foi escrito no LED. Necessário para o NRZ-I
// saber se deve inverter ou manter o estado no próximo bit.
volatile bool nivel_linha_anterior = LOW;

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Configura o Timer para a taxa de bits
  Timer1.initialize(BIT_TIME_US);
  Timer1.attachInterrupt(isr_transmite_bit);
  
  Serial.println("--- TX PRONTO ---");
  Serial.println("Digite a mensagem (max 64 chars).");
  Serial.println("Prefixo opcional 'I:' = NRZ-I, 'L:' = NRZ-L (padrao sem prefixo = NRZ-L).");
}

void loop() {
  if (Serial.available() > 0 && !transmitindo) {
    // Lê a string do monitor serial
    int bytes_lidos = Serial.readBytesUntil('\n', mensagem, 64);
    mensagem[bytes_lidos] = '\0'; // Finaliza string C
    
    if (bytes_lidos > 0) {
      uint8_t codificacao_escolhida = COD_NRZL; // Padrao (compatibilidade com a Etapa 1)
      char* msg_ptr = mensagem;
      int tamanho_msg = bytes_lidos;

      // Permite escolher a codificacao digitando "I:mensagem" ou "L:mensagem"
      if (bytes_lidos > 2 && mensagem[1] == ':') {
        if (mensagem[0] == 'I' || mensagem[0] == 'i') {
          codificacao_escolhida = COD_NRZI;
          msg_ptr = mensagem + 2;
          tamanho_msg = bytes_lidos - 2;
        } else if (mensagem[0] == 'L' || mensagem[0] == 'l') {
          codificacao_escolhida = COD_NRZL;
          msg_ptr = mensagem + 2;
          tamanho_msg = bytes_lidos - 2;
        }
      }

      preparar_frame(msg_ptr, tamanho_msg, codificacao_escolhida);
      transmitindo = true;
      Serial.print("Enviando (");
      Serial.print(codificacao_escolhida == COD_NRZI ? "NRZ-I" : "NRZ-L");
      Serial.print("): ");
      Serial.println(msg_ptr);
    }
  }
}

// ISR (Interrupt Service Routine) para controle cravado do tempo
void isr_transmite_bit() {
  if (!transmitindo) return;
  
  if (bit_atual < total_bits) {
    // Calcula qual byte e qual bit do buffer estamos lendo
    uint16_t byte_idx = bit_atual / 8;
    uint8_t bit_idx = 7 - (bit_atual % 8); // MSB primeiro
    
    bool bit_val = (buffer_tx[byte_idx] >> bit_idx) & 0x01;
    bool nivel_saida;

    if (bit_atual < BITS_FIXOS_NRZL || codificacao_atual_tx == COD_NRZL) {
      // Preambulo + SFD + Header sempre NRZ-L. Payload/CRC tambem, se essa foi
      // a codificacao escolhida (comportamento original da Etapa 1, preservado).
      nivel_saida = bit_val;
    } else if (codificacao_atual_tx == COD_NRZI) {
      // NRZ-I: bit 1 INVERTE o nivel anterior da linha, bit 0 MANTEM o nivel.
      nivel_saida = bit_val ? !nivel_linha_anterior : nivel_linha_anterior;
    } else {
      // COD_MANC (Manchester) fica a cargo do Kroda.
      nivel_saida = bit_val;
    }

    digitalWrite(LED_PIN, nivel_saida ? HIGH : LOW);
    nivel_linha_anterior = nivel_saida;

    bit_atual++;
  } else {
    // Fim da transmissão
    transmitindo = false;
    bit_atual = 0;
    digitalWrite(LED_PIN, LOW); // Garante estado de repouso
    nivel_linha_anterior = LOW;
    Serial.println("Transmissao concluida.");
  }
}

void preparar_frame(char* msg, uint8_t tamanho, uint8_t codificacao) {
  uint16_t idx = 0;

  // Guarda a codificação escolhida para que a ISR saiba como tratar o payload/CRC
  codificacao_atual_tx = codificacao;
  
  // 1. Preâmbulo
  for(int i=0; i<4; i++) buffer_tx[idx++] = PREAMBLE[i];
  
  // 2. SFD
  buffer_tx[idx++] = SFD;
  
  // 3. Header: Tipo (2 bits) + Tamanho (6 bits)
  // O tamanho vai de 0 a 63, representando 1 a 64 bytes.
  uint8_t header = (codificacao << 6) | ((tamanho - 1) & 0x3F);
  buffer_tx[idx++] = header;
  
  // 4. Payload
  for(int i=0; i<tamanho; i++) {
    buffer_tx[idx++] = (uint8_t)msg[i];
  }
  
  // 5. Calcula CRC-16 sobre Header + Payload
  // O ponteiro passa a partir do header (buffer_tx + 5)
  // Tamanho do dado pro CRC = 1 byte (header) + tamanho (payload)
  uint16_t crc_calc = calcular_crc16(&buffer_tx[5], 1 + tamanho);
  
  // Adiciona CRC ao buffer (High Byte primeiro, Low Byte depois)
  buffer_tx[idx++] = (crc_calc >> 8) & 0xFF;
  buffer_tx[idx++] = crc_calc & 0xFF;
  
  // Calcula o total de bits a serem transmitidos
  total_bits = idx * 8;
  bit_atual = 0;
}
