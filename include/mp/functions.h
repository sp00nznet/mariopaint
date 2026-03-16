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

/* Boot / System (mp_boot.c) */
void mp_008000(void);  /* Reset vector */
void mp_008013(void);  /* Hardware init */
void mp_00833B(void);  /* Register init */
void mp_00837D(void);  /* Graphics setup */
void mp_00849D(void);  /* VRAM clearing */
void mp_0084AF(void);  /* OAM init */
void mp_0084D5(void);  /* Main application init */
void mp_0080D4(void);  /* NMI handler */
void mp_00865A(void);  /* Main loop */

#endif /* MP_FUNCTIONS_H */
