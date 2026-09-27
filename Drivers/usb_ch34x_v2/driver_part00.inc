#include "RiscUsbProviderV1.h"
#include <stdint.h>
#include <stddef.h>

/* CH34x-specific matching, vendor requests and serial state live in this
 * independently installable ELF. Physical USB, enumeration, interface claims
 * and transfers remain exclusively in the usb.host/controller ELFs. */
type