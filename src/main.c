#include <stdio.h>
#include <string.h>

#include "commands.h"

static const char USAGE[] =
    "Usage: ftnpkt <command> [options]\n"
    "\n"
    "Commands:\n"
    " create <file.pkt> [packet-header options]   create an empty packet (header only)\n"
    " addmsg  <file.pkt> <text> [message options] append a message to an existing packet\n"
    " dump    <file.pkt> [dump options]           dump the packet header and all messages\n"
    "\n"
    "Run \"ftnpkt <command> -h\" for the options of a given command.\n";

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "%s", USAGE); return 2; }
    const char *cmd = argv[1];
    int    rargc = argc - 2;
    char **rargv = argv + 2;

    if (strcmp(cmd, "create") == 0) return cmd_create(rargc, rargv);
    if (strcmp(cmd, "addmsg") == 0) return cmd_addmsg(rargc, rargv);
    if (strcmp(cmd, "dump")   == 0) return cmd_dump(rargc, rargv);
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "help") == 0) {
        printf("%s", USAGE);
        return 0;
    }
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        printf("ftnpkt %s\n", FTNPKT_VERSION);
        return 0;
    }
    fprintf(stderr, "ftnpkt: unknown command \"%s\"\n", cmd);
    fprintf(stderr, "%s", USAGE);
    return 2;
}
