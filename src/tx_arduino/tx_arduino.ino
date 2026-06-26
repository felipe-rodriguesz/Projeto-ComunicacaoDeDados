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

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Configura o Timer para a taxa de bits
  Timer1.initialize(BIT_TIME_US);
  Timer1.attachInterrupt(isr_transmite_bit);
  
  Serial.println("--- TX PRONTO ---");
  Serial.println("Digite a mensagem (max 64 chars):");
}

void loop() {
  if (Serial.available() > 0 && !transmitindo) {
    // Lê a string do monitor serial
    int bytes_lidos = Serial.readBytesUntil('\n', mensagem, 64);
    mensagem[bytes_lidos] = '\0'; // Finaliza string C
    
    if (bytes_lidos > 0) {
      preparar_frame(mensagem, bytes_lidos, COD_NRZL); // Etapa 1 usa NRZ-L
      transmitindo = true;
      Serial.print("Enviando: ");
      Serial.println(mensagem);
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
    
    // Etapa 1: Apenas NRZ-L (Bit 1 = Aceso, Bit 0 = Apagado)
    // Nas próximas etapas, adicionaremos NRZ-I e Manchester aqui
    digitalWrite(LED_PIN, bit_val ? HIGH : LOW);
    
    bit_atual++;
  } else {
    // Fim da transmissão
    transmitindo = false;
    bit_atual = 0;
    digitalWrite(LED_PIN, LOW); // Garante estado de repouso
    Serial.println("Transmissao concluida.");
  }
}

void preparar_frame(char* msg, uint8_t tamanho, uint8_t codificacao) {
  uint16_t idx = 0;
  
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
