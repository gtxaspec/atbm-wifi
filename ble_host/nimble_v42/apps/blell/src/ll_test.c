/*
 * ll_test.c
 *
 *  Created on: 2021-1-12
 *      Author: NANXIAOFENG
 */
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */



#include "transport/uart/ble_hci_uart.h"
#include "nimble/ble_hci_trans.h"
#include "controller/ble_ll.h"
#include "cli.h"
#include "app_AT_cmd.h"

static TaskHandle_t Only_ll_task_h;
struct ble_npl_eventq hci_uart_evq;
extern void nimble_port_ll_task_func(void *arg);

static void nimble_ll_port_init(void)
{
	iot_printf("++nimble_ll_port_init++\n");
    os_msys_init();
    hal_timer_init(5, NULL);
    os_cputime_init(1000000);
    ble_ll_init();
	ble_hci_uart_init();
	ble_npl_eventq_init(&hci_uart_evq);
}

void
nimble_ll_test_init()
{
    xTaskCreate(nimble_port_ll_task_func, "ll", configMINIMAL_STACK_SIZE + 400,
                NULL, configMAX_PRIORITIES - 1, &Only_ll_task_h);
}

int nimble_ll_main(void)
{
	nimble_ll_port_init();
	nimble_ll_test_init();
	return 0;
}



