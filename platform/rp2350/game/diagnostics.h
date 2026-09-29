#ifndef QRP_DIAGNOSTICS_H
#define QRP_DIAGNOSTICS_H
#include <stdint.h>
/* Published once on failure; core 1 owns the LCD and can report a core 0 fault
 * without relying on USB interrupts or the framebuffer ownership protocol. */
extern volatile struct qrp_diagnostic {
    uint32_t ready, pc, lr, sp, cfsr, hfsr, bfar, mmfar;
    char message[120];
} qrp_diagnostic;
void qrp_diagnostic_error(const char *message);
void qrp_diagnostic_display(void);
#endif
