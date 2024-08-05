#ifndef __CLI_H__
#define __CLI_H__

#define 	ATBM_AT_CMD_MAX_SW_CACHE			4	//must 2 ^ n
#define		ATBM_AT_CMD_LEN_MAX					512

///#include "cmd_line.h"
struct cmd_struct{

	const char *cmd;
	int (*fn)(const char *argv);
	struct cmd_struct *next;
};


typedef struct CLI_COMMAND_DEF_S
{
    const char *    Name;
    const char *    HelpLine;
    /* See CmdLine_ReadMemory(), CmdLine_ReadMemory() and CmdLine_Dump() for example.
    */
    void            (*Handler)(char *Args);
}CLI_COMMAND_DEF;

struct cli_cmd_struct{

	char *cmd;
	void (*fn)(char *args);
	struct cli_cmd_struct *next;
};
typedef struct CMDLINE_LOCAL_S
{
	uint8_t *cmd_buf[ATBM_AT_CMD_MAX_SW_CACHE];
	uint32_t cmd_put;
	uint32_t cmd_get;
    struct cli_cmd_struct *cmds;
}CMDLINE_LOCAL; 

extern void cli_add_cmd(struct cli_cmd_struct *cmd);
extern void cli_add_cmds(struct cli_cmd_struct *cmds, int len);

/***********************************************************************/
/***                        Public Functions                         ***/
/***********************************************************************/

/**************************************************************************
**
** NAME        cli_get_token
** 
** PARAMETERS:    *pLine -    current line location to parse.
**
** RETURNS:        the token located. It never be NULL, but can be "\0" 
**              *pLine - next line location to parse.              
**
** DESCRIPTION    Locate the next token from a cli.
**
**************************************************************************/
char *cli_get_token(char **pLine);


/**************************************************************************
**
** NAME        CmdLine_GetHex
** 
** PARAMETERS:  *pLine - the current line location to parse.
**
** RETURNS:        TRUE if the next token is a hexdecimal integer.
**              *pDword - the integer returned. Unchanged if return FALSE.
**              *pLine - next line location to parse.              
**
** DESCRIPTION    Read a hexdecimal integer from a cli.
**
**************************************************************************/
int cli_get_hex(char **pLine, uint32_t *pDword);

/**************************************************************************
**
** NAME        CmdLine_GetInteger
** 
** PARAMETERS:  *pLine - the current line location to parse.
**
** RETURNS:        TRUE if the next token is an unsigned decimal integer.
**              *pDword - the integer returned. Unchanged if return FALSE.
**              *pLine - next line location to parse.              
**
** DESCRIPTION    Read an unsigned decimal integer from a cli.
**
**************************************************************************/
int cli_get_integer(char **pLine, uint32_t *pDword);

uint32_t cli_string_cmmpare(char *pStr1, const char *pStr2);

extern void cli_add_cmd(struct cli_cmd_struct *cmd);
extern void cli_add_cmds(struct cli_cmd_struct *cmds, int len);
int cli_set_event(uint8_t *data, uint16_t len);
void cli_init(void);
void cli_free(void);


#endif /* __CLI_H__ */
