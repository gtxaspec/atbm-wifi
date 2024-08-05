
#include "atbm_hal.h"
#include "os/os.h"
#include "cli.h"

static struct ble_npl_sem at_cmd_sem;
int at_cmd_sem_init = 0;
CMDLINE_LOCAL at_cmd_line;
pAtbm_thread_t cli_thread = NULL;
int cli_th_exit = 0;

extern void atcmd_init_ble(void);

char *cli_skip_space(char * line)
{
    char ch;
	int loop =0;
	
    /* escape white space */
    ch = line[0];
    while(ch != 0)
    {
        /* CmdLine_GetLine() has already replaced '\n' '\r' with 0 */
        if ( (ch == ' ') || (ch == ',') || (ch == '\t') || (ch == '='))
        {
            line++;
            ch = line[0];
			loop ++;
			if(loop >= ATBM_AT_CMD_LEN_MAX)
				break;
			
            continue;
        }
        break;
    }
    return line;
}

char *cli_get_token(char **pLine)
{
    char *    str;
    char *    line;
    char ch;
	int loop =0;
	
    line = *pLine;

    /* escape white space */
    ch = line[0];
    while(ch != 0)
    {
        /* CmdLine_GetLine() has already replaced '\n' '\r' with 0 */
        if ( (ch == ' ') || (ch == ',') || (ch == '\t'))
        {
            line++;
            ch = line[0];
			//闂冨弶顒汚T cmd 鐡掑﹦鏅�?	    	loop++;
			if(loop > 1600)
				break;
			
            continue;
        }
        break;
    }

    str = line;
    while(ch != 0)
    {
        if ( (ch == ' ') || (ch == ',') || (ch == '\t'))
        {
            line[0] = 0;
            /* CmdLine_GetLine() has replaced '\n' '\r' with 0, so we can do line++ */
            line++;
            break;
        }
        line++;
        ch = line[0];
		//闂冨弶顒汚T cmd 鐡掑﹦鏅�?    	loop++;
		if(loop > 1600)
			break;
    }

    *pLine = line;

    return str;
}

char *cli_get_token_string(char **ppLine)
{
	char *	  str;
	char *	  pLine;
	int index = 0;
	int first_flag =0;
	
	pLine = *ppLine;
	
	pLine = cli_skip_space(pLine);
	if(pLine[0] == '"'){
		first_flag=1;
		index=1;
		str = &pLine[1];
		while(pLine[index] != 0)
		{
			if (pLine[index] == '"')
			{
				first_flag++;
				//if (first_flag == 1)
				//{
				//	str = &pLine[index + 1];
				//}else
				//{
					pLine[index] = 0;
					index++;
					break;
				//}
			}
			index++;
		}
	}
		
	if (first_flag == 2)
		pLine += index;
	else{
		str = cli_get_token(&pLine);
	}
	*ppLine=pLine;
	
	return str;
}

int cli_get_hex(char **pLine, uint32_t *pDword)
{
    char *  str;
    char *  str0;
    int     got_hex;
    uint32_t  	d = 0;

    str = cli_get_token(pLine);
    if (str[0] == 0){
        return false;
    }

    str0 = str;
    got_hex = false;
    for (;;){
        char ch;

        ch = str[0];
        if (ch == 0){
            break;
        }
        if (ch >= '0' && ch <= '9'){
            d = (d<<4) | (ch - '0');
        }
        else if (ch >= 'a' && ch <= 'f'){
            d = (d<<4) | (ch - 'a' + 10);
        }
        else if (ch >= 'A' && ch <= 'F'){
            d = (d<<4) | (ch - 'A' + 10);
        }
        else{
            got_hex = false;
            break;
        }
        got_hex = true;
        str++;
    }
    if (got_hex){
        *pDword = d;
    }
    else{
        iot_printf("Invalid hexdecimal: %s\n", str0);
    }

    return got_hex;
}

int cli_get_integer(char **pLine, uint32_t *pDword)
{
    char *  str;
    char *  str0;
    int     got_int;
    uint32_t  	d = 0;

    str = cli_get_token(pLine);
    if (str[0] == 0){
        return false;
    }

    str0 = str;
    got_int = false;
    for (;;){
        char ch;

        ch = str[0];
        if(ch == 0){
            break;
        }
        if(ch >= '0' && ch <= '9'){
            d = d*10 + (ch - '0');
            got_int = true;
            str++;
        }
        else{
            got_int = false;
            break;
        }
    }
    if (got_int){
        *pDword = d;
    }
    else{
        iot_printf("Invalid unsigned decimal: %s\n", str0);
    }

    return got_int;
}

int cli_get_sign_integer(char **pLine, int *pDword)
{
    char *  str;
    char *  str0;
    int     got_int;
	int negativeFlag = 0;
    int  d = 0;

    str = cli_get_token(pLine);
    if (str[0] == 0){
        return false;
    }

    str0 = str;
    got_int = false;
    for (;;){
        char ch;

        ch = str[0];
        if (ch == 0){
            break;
        }
		if((ch == '-') && (str0 == str)){
			negativeFlag = -1;
            str++;
		}
		else if (ch >= '0' && ch <= '9'){
            d = d*10 + (ch - '0');
            got_int = true;
            str++;
        }
        else{
            got_int = false;
            break;
        }
    }
    if (got_int){
    	if (negativeFlag < 0)
        	*pDword = d * negativeFlag;
    	else
    		*pDword = d;	
    }
    else{
        iot_printf("Invalid unsigned decimal: %s\n", str0);
    }

    return got_int;
}

void cli_help(char *arg)
{
	struct cli_cmd_struct *cmd = at_cmd_line.cmds;

	iot_printf( "\nSupported commands:\n");
	do {
		iot_printf(cmd->cmd);
		iot_printf("\n");
		cmd = cmd->next;

	} while(cmd);

	return;
}

void cli_add_cmd(struct cli_cmd_struct *cmd)
{
	cmd->next = at_cmd_line.cmds;
	at_cmd_line.cmds = cmd;
}

void cli_add_cmds(struct cli_cmd_struct *cmds, int len)
{
	int i;

	for (i = 0; i < len; i++){
		cli_add_cmd(cmds++);
	}
}

uint32_t cli_string_cmmpare(char *pStr1, const char *pStr2)
{
	while(*pStr1){
		if(*pStr1 != *pStr2){
			return false;
		}
		pStr1 ++;
		pStr2 ++;
	}
	return *pStr2 == 0;
}

int cli_process_cmd(char * Line)
{  
	struct cli_cmd_struct *p = at_cmd_line.cmds;
    char *Token;
	
    Token = cli_get_token(&Line);

    if (Token[0] == 0){
        return -1;
    }

	while (p) {
		if (cli_string_cmmpare(Token, p->cmd)){
			Line = cli_skip_space(Line);
			p->fn(Line);
			return 1;
		}

		p = p->next;
	}

	iot_printf("Unknown cmd: %s\n", Token);
	return 0;
}



/**************************************************************************
**
** NAME     CmdLine_ProcessingInput
**
** PARAMETERS:
**
** RETURNS:
**
** DESCRIPTION  Processing input and execute commands in non-IRQ context.
**
**************************************************************************/
void cli_processing_input(void)
{
	uint32_t get;

	for (;;)
	{
		char *Line;

		//ATBM_ASSERT(at_cmd_line.cmd_get <= at_cmd_line.cmd_put)
		if(at_cmd_line.cmd_get == at_cmd_line.cmd_put){
			break; 
		}
		
		get = at_cmd_line.cmd_get & (ATBM_AT_CMD_MAX_SW_CACHE - 1);
		Line = at_cmd_line.cmd_buf[get];

		cli_process_cmd(Line);
		
		at_cmd_line.cmd_get ++;
	}    
}

struct cli_cmd_struct GenericCommands[] =
{
	{.cmd ="help",	.fn = cli_help,		.next = (void*)0 },		
};


int cli_main(void *param)
{
	int i;
	
	for(i=0; i<ATBM_AT_CMD_MAX_SW_CACHE; i++){
		at_cmd_line.cmd_buf[i] = atbm_kzalloc(ATBM_AT_CMD_LEN_MAX, GFP_KERNEL);
		ATBM_BUG_ON(at_cmd_line.cmd_buf[i] == NULL);
	}
	at_cmd_line.cmd_get = 0;
	at_cmd_line.cmd_put = 0;
	
    cli_add_cmds(&GenericCommands[0],
		sizeof(GenericCommands)/sizeof(GenericCommands[0]));
	ble_npl_sem_init(&at_cmd_sem, 0);
	at_cmd_sem_init = 1;
	
	while (1) {
		ble_npl_sem_pend(&at_cmd_sem, BLE_NPL_TIME_FOREVER);
		if(cli_th_exit){
			break;
		}
		cli_processing_input();
    }
	atbm_ThreadStopEvent(cli_thread);
	at_cmd_sem_init = 0;
	return 0;
}

int cli_set_event(uint8_t *data, uint16_t len)
{
	uint32_t put;
	os_sr_t sr;
	
	if((at_cmd_line.cmd_put - at_cmd_line.cmd_get) >= ATBM_AT_CMD_MAX_SW_CACHE){
		iot_printf("atcmd software cache full.\n");
		return -1;		
	}
	
	if(len > ATBM_AT_CMD_LEN_MAX){
		iot_printf("atcmd length is too large.\n");
		return -1;			
	}
	
	OS_ENTER_CRITICAL(sr);
	put = at_cmd_line.cmd_put & (ATBM_AT_CMD_MAX_SW_CACHE - 1);
	ATBM_ASSERT(at_cmd_line.cmd_buf[put] != NULL);
	memset(at_cmd_line.cmd_buf[put], 0, ATBM_AT_CMD_LEN_MAX);
	memcpy(at_cmd_line.cmd_buf[put], data, len);
	at_cmd_line.cmd_put ++;
	OS_EXIT_CRITICAL(sr);
	
	if(at_cmd_sem_init){
		ble_npl_sem_release(&at_cmd_sem);
	}
	
	return 0;
}

void cli_init(void)
{
	cli_th_exit = 0;
	cli_thread = atbm_createThread(cli_main, (atbm_void*)ATBM_NULL, BLE_AT_PRIO);
}

void cli_free(void)
{
	int i;
	
	if(cli_thread){
		cli_th_exit = 1;
		ble_npl_sem_release(&at_cmd_sem);
	}

	while(at_cmd_sem_init){
		ble_npl_time_delay(ble_npl_time_ms_to_ticks32(10));
	}
	
	atbm_stopThread(cli_thread);
	cli_thread = NULL;	
	ble_npl_sem_free(&at_cmd_sem);

	for(i=0; i<ATBM_AT_CMD_MAX_SW_CACHE; i++){
		atbm_kfree(at_cmd_line.cmd_buf[i]);

	}
}


