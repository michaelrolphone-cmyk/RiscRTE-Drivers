/* Included after pinned HCD private declarations. The single provider executor
 * owns task calls; HCD critical sections serialize those calls with its ISR. */
static esp_err_t risc_hcd_halt_poll_locked(pipe_t *pipe)
{
    if (pipe->cs_flags.risc_halt_invalidated) {
        return pipe->port->flags.conn_dev_ena ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (!pipe->port->flags.conn_dev_ena) {
        /* The HAL's documented port-error path requires no channel interrupt.
         * A late already-latched interrupt must not parse a flushed buffer. */
        usb_dwc_hal_chan_mark_halted(pipe->chan_obj);
        pipe->chan_obj->flags.halt_requested = 0;
        pipe->state = HCD_PIPE_STATE_HALTED;
        pipe->cs_flags.risc_halt_invalidated = 1;
        pipe->cs_flags.risc_halt_pending = 0;
        pipe->cs_flags.waiting_halt = 0;
        return ESP_OK;
    }
    if (pipe->cs_flags.risc_halt_pending) return ESP_ERR_NOT_FINISHED;
    if (pipe->state == HCD_PIPE_STATE_HALTED) return ESP_OK;
    if (pipe->state != HCD_PIPE_STATE_ACTIVE) return ESP_ERR_INVALID_STATE;
    pipe->cs_flags.risc_halt_pending = 1;
    pipe->cs_flags.waiting_halt = 1;
    if (usb_dwc_hal_chan_request_halt(pipe->chan_obj)) {
        /* No executing channel and no pending completion interrupt. */
        pipe->cs_flags.risc_halt_pending = 0;
        pipe->cs_flags.waiting_halt = 0;
        pipe->state = HCD_PIPE_STATE_HALTED;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t risc_usb_hcd_halt_poll(hcd_pipe_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    pipe_t *pipe = (pipe_t *)handle;
    HCD_ENTER_CRITICAL();
    esp_err_t rc;
    if (pipe->cs_flags.pipe_cmd_processing || pipe->task_waiting_pipe_notif) {
        rc = ESP_ERR_NOT_FINISHED;
    } else {
        rc = risc_hcd_halt_poll_locked(pipe);
    }
    HCD_EXIT_CRITICAL();
    return rc;
}

esp_err_t risc_usb_hcd_endpoint_command(hcd_pipe_handle_t handle, hcd_pipe_cmd_t command)
{
    if (!handle || (command != HCD_PIPE_CMD_FLUSH && command != HCD_PIPE_CMD_CLEAR))
        return ESP_ERR_INVALID_ARG;
    pipe_t *pipe = (pipe_t *)handle;
    HCD_ENTER_CRITICAL();
    if (pipe->cs_flags.pipe_cmd_processing || pipe->cs_flags.risc_halt_pending ||
        pipe->task_waiting_pipe_notif) {
        HCD_EXIT_CRITICAL();
        return ESP_ERR_NOT_FINISHED;
    }
    pipe->cs_flags.pipe_cmd_processing = 1;
    esp_err_t rc = command == HCD_PIPE_CMD_FLUSH ? _pipe_cmd_flush(pipe) : _pipe_cmd_clear(pipe);
    pipe->cs_flags.pipe_cmd_processing = 0;
    HCD_EXIT_CRITICAL();
    return rc;
}
