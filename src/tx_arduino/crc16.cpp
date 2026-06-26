#include "crc16.h"

uint16_t calcular_crc16(const uint8_t *dados, uint16_t tamanho) {
    uint16_t crc = 0xFFFF; // Valor inicial CCITT
    
    for (uint16_t i = 0; i < tamanho; i++) {
        crc ^= ((uint16_t)dados[i] << 8); // Traz o byte para a parte alta
        
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                // Se o MSB for 1, desloca e faz XOR com o polinômio 0x1021
                crc = (crc << 1) ^ 0x1021;
            } else {
                // Senão, apenas desloca
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}
