/* ESP32-S3 controller-owned USB PHY pad configuration.
 *
 * The pinned IDF v4.4.7 usb_phy.c calls gpio_set_drive_capability() only for
 * USBPHY_DM_NUM and USBPHY_DP_NUM when initializing its internal PHY. Pulling
 * the firmware's complete GPIO driver into this ELF would also duplicate an
 * unrelated GPIO ISR service and its mutable state. Implement precisely this
 * PHY-specific operation here instead; never forward it to resident firmware.
 *
 * The provider MUST own the OTG role/pins exclusively before this executes.
 * Board-power and generic resource arbitration are separate install gates.
 */
#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <soc/soc_caps.h>
#include <soc/usb_pins.h>

#if !CONFIG_IDF_TARGET_ESP32S3 || !SOC_GPIO_SUPPORT_RTC_INDEPENDENT
#error "This USB PHY pad implementation is only valid on ESP32-S3 digital GPIO"
#endif

esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t strength)
{
    if ((pin != USBPHY_DM_NUM && pin != USBPHY_DP_NUM) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(pin) || strength >= GPIO_DRIVE_CAP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Same ESP32-S3 digital pad register operation used by the upstream
     * gpio_set_drive_capability() -> gpio_hal -> gpio_ll implementation.
     * Single provider executor serializes PHY initialization/teardown.
     */
    gpio_ll_set_drive_capability(&GPIO, pin, strength);
    return ESP_OK;
}
