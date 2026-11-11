// Wrap only the inert CPU test target's actual miner.cu entry. No production test hook or simulated UI.
#ifdef main
#undef main
#endif
#include "console_completion_fixture.h"
int canaryConsoleInertMain(int,char **);
int main(int argc,char **argv){const int result=canaryConsoleInertMain(argc,argv);console_completion_fixture::complete();return result;}
