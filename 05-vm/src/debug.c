#include "vm.h"
#include <stdio.h>

/*
 * Lantern VM — Disassembler
 *
 * Turns bytecode back into human-readable instructions.
 * Essential for debugging — you can't fix what you can't see.
 */

void vm_disassemble(const uint8_t *code, size_t size) {
    for (size_t i = 0; i < size; i += 2) {
        if (i + 1 >= size) {
            printf("%04zX: %02X??\n", i, code[i]);
            break;
        }

        uint16_t opcode = ((uint16_t)code[i] << 8) | (uint16_t)code[i + 1];
        uint8_t  high  = (opcode >> 12) & 0xF;
        uint16_t nnn   = opcode & 0x0FFF;
        uint8_t  x     = (opcode >> 8) & 0xF;
        uint8_t  y     = (opcode >> 4) & 0xF;
        uint8_t  kk    = opcode & 0xFF;
        uint8_t  n     = opcode & 0xF;

        printf("%04zX: %04X  ", i, opcode);

        switch (high) {
        case 0x0:
            if (opcode == 0x00E0)      printf("CLR");
            else if (opcode == 0x00EE) printf("RET");
            else                        printf("SYS %03X", nnn);
            break;
        case 0x1: printf("JP %03X", nnn);            break;
        case 0x2: printf("CALL %03X", nnn);           break;
        case 0x3: printf("SE V%X, %02X", x, kk);     break;
        case 0x4: printf("SNE V%X, %02X", x, kk);   break;
        case 0x5: printf("SE V%X, V%X", x, y);      break;
        case 0x6: printf("LD V%X, %02X", x, kk);     break;
        case 0x7: printf("ADD V%X, %02X", x, kk);   break;
        case 0x8:
            switch (n) {
            case 0x0: printf("LD V%X, V%X", x, y);       break;
            case 0x1: printf("OR V%X, V%X", x, y);        break;
            case 0x2: printf("AND V%X, V%X", x, y);       break;
            case 0x3: printf("XOR V%X, V%X", x, y);       break;
            case 0x4: printf("ADD V%X, V%X", x, y);       break;
            case 0x5: printf("SUB V%X, V%X", x, y);       break;
            case 0x6: printf("SHR V%X", x);                break;
            case 0x7: printf("SUBN V%X, V%X", x, y);      break;
            case 0xE: printf("SHL V%X", x);                break;
            default:  printf("??? 8xy%X", n);               break;
            }
            break;
        case 0x9: printf("SNE V%X, V%X", x, y);      break;
        case 0xA: printf("LD I, %03X", nnn);          break;
        case 0xB: printf("JP V0, %03X", nnn);         break;
        case 0xC: printf("RND V%X, %02X", x, kk);    break;
        case 0xD: printf("DRW V%X, V%X, %X", x, y, n); break;
        case 0xE:
            if (kk == 0x9E)      printf("SKP V%X", x);
            else if (kk == 0xA1) printf("SKNP V%X", x);
            else                  printf("??? Ex%X", kk);
            break;
        case 0xF:
            switch (kk) {
            case 0x07: printf("LD V%X, DT", x);           break;
            case 0x0A: printf("LD V%X, K", x);             break;
            case 0x15: printf("LD DT, V%X", x);            break;
            case 0x18: printf("LD ST, V%X", x);            break;
            case 0x1E: printf("ADD I, V%X", x);            break;
            case 0x29: printf("LD F, V%X", x);             break;
            case 0x33: printf("LD B, V%X", x);             break;
            case 0x55: printf("LD [I], V0-V%X", x);       break;
            case 0x65: printf("LD V0-V%X, [I]", x);       break;
            default:   printf("??? Fx%02X", kk);           break;
            }
            break;
        }
        printf("\n");
    }
}