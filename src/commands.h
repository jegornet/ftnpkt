#ifndef FTNPKT_COMMANDS_H
#define FTNPKT_COMMANDS_H

/* Each subcommand receives only its own arguments (argv[0] excluded). */
int cmd_create(int argc, char **argv);
int cmd_addmsg(int argc, char **argv);
int cmd_dump(int argc, char **argv);

#endif /* FTNPKT_COMMANDS_H */
