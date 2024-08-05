#ifndef ATBM_OS_IW_WSM_H
#define ATBM_OS_IW_WSM_H


#include "atbm_wsm.h"

int wsm_set_rts(struct atbmwifi_common *hw_priv, atbm_uint32 rts, int if_id);
int wsm_get_rts(struct atbmwifi_common *hw_priv, atbm_uint32 *rts, int if_id);
int wsm_set_power(struct atbmwifi_common *hw_priv, atbm_uint32 power, int if_id);
int wsm_get_power(struct atbmwifi_common *hw_priv, atbm_uint32 *power, int if_id);
int wsm_set_default_key(struct atbmwifi_common *hw_priv, atbm_uint32 key_id, int if_id);


#endif /* ATBM_OS_IW_WSM_H */

