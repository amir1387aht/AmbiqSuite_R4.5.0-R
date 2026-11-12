/* SPDX-License-Identifier: GPL-2.0-only
 * Serialize storage work onto one dedicated YAFFS/flash owner task. */
#include "mb8_storage.h"
#include "mb8_storage_log.h"
#include "am_mcu_apollo.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "watch_gpu.h"

struct storage_request
{
    mb8_storage_job_fn fn;
    void *context;
    int result;
    SemaphoreHandle_t completed;
};
static TaskHandle_t worker;
static QueueHandle_t requests;

bool mb8_storage_is_worker(void)
{
    return worker && xTaskGetCurrentTaskHandle() == worker;
}

static void storage_worker(void *unused)
{
    (void)unused;
    /* The NAND DMA buffer uses the initialized shared-SRAM infrastructure. */
    while (!watch_gpu_ready)
        vTaskDelay(pdMS_TO_TICKS(10));
    for (;;)
    {
        struct storage_request *request;
        if (xQueueReceive(requests, &request, portMAX_DELAY) == pdTRUE)
        {
            request->result = request->fn(request->context);
            BaseType_t signalled = xSemaphoreGive(request->completed);
            configASSERT(signalled == pdTRUE);
            (void)signalled;
        }
    }
}

int mb8_storage_execute(mb8_storage_job_fn fn, void *context)
{
    if (!fn || __get_IPSR() || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !worker || !requests)
        return -1;
    if (mb8_storage_is_worker())
        return fn(context);
    struct storage_request request = { .fn = fn, .context = context, .result = -1 };
    request.completed = xSemaphoreCreateBinary();
    if (!request.completed)
        return -1;
    struct storage_request *ptr = &request;
    if (xQueueSend(requests, &ptr, portMAX_DELAY) != pdTRUE)
    {
        vSemaphoreDelete(request.completed);
        return -1;
    }
    /* Caller and context must remain alive until completion. Do not cancel or
     * delete a task while it is executing a synchronous storage request. */
    while (xSemaphoreTake(request.completed, portMAX_DELAY) != pdTRUE) {}
    int result = request.result;
    vSemaphoreDelete(request.completed);
    return result;
}

void mb8_storage_startup(void)
{
    if (worker || requests)
        return;
    requests = xQueueCreate(8, sizeof(struct storage_request *));
    if (!requests)
    {
        mb8_storage_log("[NAND] storage queue allocation failed\r\n");
        return;
    }
    if (xTaskCreate(storage_worker, "NAND storage", 1536, NULL,
                    tskIDLE_PRIORITY + 1, &worker) != pdPASS)
    {
        vQueueDelete(requests);
        requests = NULL;
        mb8_storage_log("[NAND] storage task allocation failed\r\n");
    }
}
