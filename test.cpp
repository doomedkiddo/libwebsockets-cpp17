#include "libwebsockets.h"
int wsCallback(struct lws* wsi,
                         enum lws_callback_reasons reason,
                         void* user,
                         void* in,
                         size_t len);
int main(){
    return 0;
}
