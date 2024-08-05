#ifndef __SHELL_H__
#define __SHELL_H__

struct shell_param{
	char * param;
	char * param_desc;
};

struct shell_cmd_help{
	const char * summary;
	const char * usage;
	const struct shell_param * params;
};
struct shell_cmd {
	const char *sc_cmd;
	int (*sc_cmd_func)(int argc,char ** argv);
	const struct shell_cmd_help * help;
};

void ble_mesh_shell_init(void);

#endif
