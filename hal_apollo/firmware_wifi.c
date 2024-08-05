
#include "apollo.h"
#include "fwio.h"
#ifdef CONFIG_USE_FW_H

#ifdef USB_BUS
#include "firmware_usb.h"
#endif
#ifdef SDIO_BUS
#include "firmware_sdio.h"
#endif
#ifdef SPI_BUS
#include "firmware_spi.h"
#endif




int load_usb_wifi_firmware(struct firmware_altobeam *fw_altobeam)
{

	fw_altobeam->hdr.iccm_len = sizeof(fw_code);
	fw_altobeam->hdr.dccm_len = sizeof(fw_data);

	fw_altobeam->fw_iccm = (u8 *)&fw_code[0];
	fw_altobeam->fw_dccm = (u8 *)&fw_data[0];

	return 0;
}

#endif

