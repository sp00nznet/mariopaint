/*
 * Mario Paint — recompiled function declarations.
 *
 * Functions are named mp_XXXXXX where XXXXXX is the 24-bit SNES address.
 * Addresses are from the Mario Paint (JU) ROM (LoROM).
 */
#ifndef MP_FUNCTIONS_H
#define MP_FUNCTIONS_H

/* Register all recompiled functions in the dispatch table */
void mp_register_all(void);

/*
 * Boot / System
 * TODO: Identify and recompile these from the disassembly
 */

/* Reset vector entry point */
/* void mp_00XXXX(void); */

/* NMI handler */
/* void mp_00XXXX(void); */

/* Main loop */
/* void mp_00XXXX(void); */

#endif /* MP_FUNCTIONS_H */
