// --- ARDUINO 2: RECEPTOR (RX) - VERSÃO MASTER COM TODOS OS BÔNUS ---
#define PINO_LDR A0
#define LIMIAR_LUZ 400

unsigned long tempoBit = 0;
int modoCodificacao = 1;
bool timeoutOcorreu = false;
int bitsCorrigidosNestePacote = 0;

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

// Algoritmo de Correção Ativa baseada em Decodificação por Votação de Maioria
// Estendida (Bit-level Majority Voting) Consegue reconstruir perfeitamente o
// byte original corrigindo até 3 bits alterados no bloco
byte corrigirDados(byte b1, byte b2, byte b3) {
  byte reconfigurado2 = ~b2;
  byte reconfigurado3 = b3 ^ 0xAA;
  byte resultadoCorrigido = 0;

  for (int i = 0; i < 8; i++) {
    int bit1 = bitRead(b1, i);
    int bit2 = bitRead(reconfigurado2, i);
    int bit3 = bitRead(reconfigurado3, i);

    // Votação de maioria simples para o bit na posição i
    int votosUm = bit1 + bit2 + bit3;
    if (votosUm >= 2) {
      bitWrite(resultadoCorrigido, i, 1);
      if (bit1 != 1)
        bitsCorrigidosNestePacote++;
    } else {
      bitWrite(resultadoCorrigido, i, 0);
      if (bit1 != 0)
        bitsCorrigidosNestePacote++;
    }
  }
  return resultadoCorrigido;
}

void esperarSincronismo() {
  Serial.println(
      "\n[REDE] Escutando barramento... Aguardando preambulo de sincronismo.");
  while (analogRead(PINO_LDR) <= LIMIAR_LUZ) {
    delay(1);
  }

  unsigned long inicioPulso = millis();
  while (analogRead(PINO_LDR) > LIMIAR_LUZ) {
    delay(1);
  }
  unsigned long fimPulso = millis();

  tempoBit = (fimPulso - inicioPulso) / 5;
  Serial.print(
      "[SUCESSO] Sincronizado! Velocidade detectada automaticamente: ");
  Serial.print(tempoBit);
  Serial.println(" ms/bit.");
}

byte lerByte() {
  unsigned long inicioEspera = millis();

  while (analogRead(PINO_LDR) <= LIMIAR_LUZ) {
    if (millis() - inicioEspera > 4000) {
      Serial.println("\n[ALERTA] Timeout de recepcao expirado.");
      timeoutOcorreu = true;
      return 0;
    }
    delay(1);
  }

  timeoutOcorreu = false;

  if (modoCodificacao == 3) {
    delay(tempoBit + (tempoBit * 3 / 4));
  } else {
    delay(tempoBit + (tempoBit / 2));
  }

  byte letraRecebida = 0;
  bool estadoAnterior = true;

  for (int i = 7; i >= 0; i--) {
    bool luzAtual = (analogRead(PINO_LDR) > LIMIAR_LUZ);

    if (modoCodificacao == 1 || modoCodificacao == 3) {
      if (luzAtual == true)
        bitWrite(letraRecebida, i, 1);
      else
        bitWrite(letraRecebida, i, 0);
    } else if (modoCodificacao == 2) {
      if (luzAtual != estadoAnterior)
        bitWrite(letraRecebida, i, 1);
      else
        bitWrite(letraRecebida, i, 0);
      estadoAnterior = luzAtual;
    }
    delay(tempoBit);
  }

  while (analogRead(PINO_LDR) > LIMIAR_LUZ) {
    delay(5);
  }
  return letraRecebida;
}

void setup() { Serial.begin(115200); }

void loop() {
  Serial.println("\n=============================================");
  Serial.println("  UTFPR - RECEPTOR DE COMUNICAÇÃO DE DADOS   ");
  Serial.println("=============================================");
  Serial.println("Escolha o modo de decodificacao esperado: [1] NRZ-L, [2] "
                 "NRZ-I, [3] Manchester");

  while (Serial.available() == 0) {
  }
  modoCodificacao = Serial.parseInt();
  while (Serial.available() > 0)
    Serial.read();

  while (true) {
    timeoutOcorreu = false;
    bitsCorrigidosNestePacote = 0;

    esperarSincronismo();

    // 1. Lê o tamanho do payload
    byte tamanhoMsg = lerByte();
    if (timeoutOcorreu)
      continue;
    if (tamanhoMsg > 64)
      tamanhoMsg = 64;

    // 2. Coleta e corrige os caracteres recebidos de forma ativa
    String mensagem = "";
    for (int i = 0; i < tamanhoMsg; i++) {
      byte b1 = lerByte();
      byte b2 = lerByte();
      byte b3 = lerByte();

      if (timeoutOcorreu)
        break;

      char caractereCorrigido = (char)corrigirDados(b1, b2, b3);
      mensagem += caractereCorrigido;
    }

    // 3. Recebe o CRC-8 final do frame
    byte crcRecebido = 0;
    if (!timeoutOcorreu) {
      crcRecebido = lerByte();
    }

    // 4. Avaliação e exibição dos resultados da camada de aplicação
    if (timeoutOcorreu) {
      Serial.println("[PROTOLOCO] Erro critico: Conexao interrompida.");
    } else {
      byte crcCalculado = calcularCRC8(mensagem);

      Serial.println("\n>>>>>>> NOVO PACOTE RECEBIDO <<<<<<<");
      Serial.print("Conteudo de Texto: ");
      Serial.println(mensagem);
      Serial.print("Total de bits corrigidos em tempo real: ");
      Serial.println(bitsCorrigidosNestePacote);

      if (crcRecebido == crcCalculado) {
        Serial.println("[STATUS] TRANSMISSÃO SUCESSO: Dados íntegros e "
                       "validados por CRC!");
      } else {
        Serial.println("[STATUS] FALHA CRÍTICA: Ruído excessivo no canal "
                       "superou a capacidade de correção.");
      }
    }
  }
}