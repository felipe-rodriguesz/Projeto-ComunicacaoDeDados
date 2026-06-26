#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

// Calcula o CRC-16-CCITT (Polinômio: 0x1021, Valor Inicial: 0xFFFF)
// Parâmetros:
//   - dados: ponteiro para o array de bytes (payload ou header + payload)
//   - tamanho: quantidade de bytes
// Retorna:
//   - O valor de 16 bits correspondente ao CRC
uint16_t calcular_crc16(const uint8_t *dados, uint16_t tamanho);

#endif
