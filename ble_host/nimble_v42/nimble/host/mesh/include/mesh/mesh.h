/** @file
 *  @brief Bluetooth Mesh Profile APIs.
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BT_MESH_H
#define __BT_MESH_H


#include "syscfg/syscfg.h"
#include "os/os_mbuf.h"

#include "glue.h"
#include "access.h"
#include "main.h"
#include "cfg_srv.h"
#include "health_srv.h"
#include "cfg_cli.h"
#include "health_cli.h"
#include "proxy.h"

bool bt_mesh_is_provisioner_en(void);
int bt_mesh_provisioner_enable(bt_mesh_prov_bearer_t bearers);
int bt_mesh_provisioner_disable(bt_mesh_prov_bearer_t bearers);


#endif /* __BT_MESH_H */
