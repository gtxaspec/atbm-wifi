#include "atbm_hal.h"

#include "atbm_os_sdio.h"

extern int atbm_reg_read_8(struct atbmwifi_common *hw_priv, atbm_uint16 addr, atbm_uint8 *val);
extern int atbm_reg_write_8(struct atbmwifi_common *hw_priv, atbm_uint16 addr, atbm_uint8 val);

int atbm_sdio_register(struct atbm_sdio_driver *sdio_driver)
{
	return sdio_register_driver(sdio_driver);
}

void atbm_sdio_deregister(struct atbm_sdio_driver *sdio_driver)
{
	sdio_unregister_driver(sdio_driver);
}

void atbm_sdio_claim_host(struct atbm_sdio_func *func)
{
	sdio_claim_host(func);
}

void atbm_sdio_release_host(struct atbm_sdio_func *func)
{
	sdio_release_host(func);
}

atbm_int32 atbm_sdio_enable_func(struct atbm_sdio_func *func)
{
	sdio_enable_func(func);
	return 0;
}

void atbm_sdio_disable_func(struct atbm_sdio_func *func)
{
	sdio_disable_func(func);
}

void atbm_sdio_set_drvdata(struct atbm_sdio_func *func,void *priv)
{
	sdio_set_drvdata(func,priv);
}

void *atbm_sdio_get_drvdata(struct atbm_sdio_func *func)
{
	return sdio_get_drvdata(func);
}
#ifndef CONFIG_ATBM_BLE_PLATFORM

extern struct atbmwifi_common g_hw_prv;
#endif
int atbm_sdio_claim_irq(struct atbm_sdio_func *func,void (*irq_handle)(struct atbm_sdio_func *func))
{
	atbm_uint32 ret;

	sdio_claim_host(func);
	ret = sdio_claim_irq(func, irq_handle);
	sdio_release_host(func);

	return ret;
}

int atbm_sdio_release_irq(struct atbm_sdio_func *func)
{
	atbm_uint32 ret;

	sdio_claim_host(func);
	ret = sdio_release_irq(func);
	sdio_release_host(func);

	return ret;
}

int __atbm_sdio_memcpy_fromio(struct atbm_sdio_func *func,void *dst,unsigned int addr,int count)
{
	return sdio_memcpy_fromio(func, dst, addr, count);
}

int __atbm_sdio_memcpy_toio(struct atbm_sdio_func *func,unsigned int addr,void *src,int count)
{
	return sdio_memcpy_toio(func, addr, (void *)src, count);
}

unsigned char atbm_sdio_f0_readb(struct atbm_sdio_func *func,unsigned int addr,int *retval)
{
	unsigned char date = 0;
	bool ret;
	struct sbus_priv *priv = sdio_get_drvdata(func);
	ret = atbm_reg_read_8(priv->core, addr, &date);
	*retval = 0;
	if(ret == false)
		*retval = -1;

	return date;
}

void atbm_sdio_f0_writeb(struct atbm_sdio_func *func,unsigned char regdata,unsigned int addr,int *retval)
{
	bool ret;
	struct sbus_priv *priv = sdio_get_drvdata(func);

	ret = atbm_reg_write_8(priv->core, addr, regdata);
	*retval = 0;
	if(ret == false)
		*retval = -1;

	return;
}

atbm_uint32 atbm_sdio_alignsize(struct atbm_sdio_func *func,atbm_uint32 size)
{
	atbm_uint32 aligned = sdio_align_size(func, size);
	return aligned;
}

int  atbm_sdio_set_blocksize(struct atbm_sdio_func *func,int blocksize)
{
	return sdio_set_block_size(func, blocksize);
}

