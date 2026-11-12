#include "pool_tls_stream_service.h"
int main(int argc,char **argv){return om::runPoolTlsStreamService(argc,argv,om::openPoolTlsConnection,om::openPoolPlaintextConnection);}
