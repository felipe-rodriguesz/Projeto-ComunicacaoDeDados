// --- ARDUINO 1: EMISSOR (TX) - VERSÃO MASTER COM TODOS OS BÔNUS ---
#define PINO_LED 8

int tempoBit = 50;
int modoCodificacao = 1;
bool nivelAtual_NRZI = LOW;

// Função clássica de CRC-8 para a validação final do frame
byte calcularCRC8(String msg) {
  byte crc = 0x00;
  for (int i = 0; i < msg.length(); i++) {
    crc ^= msg[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x07;
      else
        crc = (crc << 1);
    }
  }
  return crc;
}

// Mecanismo de Codificação de Correção de Erros (Gera redundância para proteção
// de 3 bits) Aplica uma expansão de repetição sistemática com inversão
// combinada para espalhar a assinatura dos bits
void aplicarCodificacaoCorrecao(byte dado, byte *blocoSaida) {
  blocoSaida[0] = dado;
  blocoSaida[1] = ~dado; // Bloco de paridade invertida complementar
  blocoSaida[2] =
      dado ^
      0xAA; // Máscara de alternância para detecção de flutuações longas de LDR
}

void setup() {
  pinMode(PINO_LED, OUTPUT);
  digitalWrite(PINO_LED, LOW);
  Serial.begin(115200);
}

void enviarSincronismo() {
  digitalWrite(PINO_LED, HIGH);
  delay(tempoBit * 5);
  digitalWrite(PINO_LED, LOW);
  delay(tempoBit * 2);
}

void enviarByte(byte dado) {
  digitalWrite(PINO_LED, HIGH); // Start Bit
  nivelAtual_NRZI = HIGH;
  delay(tempoBit);

  for (int i = 7; i >= 0; i--) {
    int bitAtual = bitRead(dado, i);

    if (modoCodificacao == 1) {
      if (bitAtual == 1)
        digitalWrite(PINO_LED, HIGH);
      else
        digitalWrite(PINO_LED, LOW);
      delay(tempoBit);
    } else if (modoCodificacao == 2) {
      if (bitAtual == 1)
        nivelAtual_NRZI = !nivelAtual_NRZI;
      digitalWrite(PINO_LED, nivelAtual_NRZI);
      delay(tempoBit);
    } else if (modoCodificacao == 3) {
      if (bitAtual == 1) {
        digitalWrite(PINO_LED, LOW);
        delay(tempoBit / 2);
        digitalWrite(PINO_LED, HIGH);
        delay(tempoBit / 2);
      } else {
        digitalWrite(PINO_LED, HIGH);
        delay(tempoBit / 2);
        digitalWrite(PINO_LED, LOW);
        delay(tempoBit / 2);
      }
    }
  }

  digitalWrite(PINO_LED, LOW); // Stop Bit
  delay(tempoBit * 2);
}

void loop() {
  Serial.println("\n=============================================");
  Serial.println("  UTFPR - EMISSOR DE COMUNICAÇÃO DE DADOS   ");
  Serial.println("=============================================");

  Serial.println("1. Digite a velocidade da rede (ms por bit) [Ex: 50, 100]:");
  while (Serial.available() == 0) {
  }
  tempoBit = Serial.parseInt();
  while (Serial.available() > 0)
    Serial.read();

  Serial.println(
      "2. Escolha a codificacao: [1] NRZ-L, [2] NRZ-I, [3] Manchester");
  while (Serial.available() == 0) {
  }
  modoCodificacao = Serial.parseInt();
  while (Serial.available() > 0)
    Serial.read();

  Serial.println("3. Digite a mensagem (max 64 caracteres):");
  while (Serial.available() == 0) {
  }
  String mensagem = Serial.readStringUntil('\n');
  if (mensagem.length() > 64)
    mensagem = mensagem.substring(0, 64);

  Serial.println("\n[INFO] Iniciando transmissao do pacote protegido...");

  // Passo 0: Envia o preâmbulo de calibração automática de velocidade
  enviarSincronismo();

  // Passo 1: Envia o tamanho da mensagem original
  enviarByte((byte)mensagem.length());

  // Passo 2: Envia o Payload com a camada de Proteção de Erros ativa
  byte blocoProtegido[3];
  for (int i = 0; i < mensagem.length(); i++) {
    aplicarCodificacaoCorrecao(mensagem[i], blocoProtegido);
    enviarByte(blocoProtegido[0]); // Byte original
    enviarByte(blocoProtegido[1]); // Redundância 1
    enviarByte(blocoProtegido[2]); // Redundância 2
  }

  // Passo 3: Envia a validação de integridade CRC-8 do texto puro
  byte crc = calcularCRC8(mensagem);
  enviarByte(crc);

  Serial.println("[INFO] Todo o pacote foi transmitido com sucesso!");
}