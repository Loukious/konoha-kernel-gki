#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
typedef uint8_t u8;typedef uint32_t u32;
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define CAT2(a,b) a##b
#define CAT(a,b) CAT2(a,b)
static pthread_mutex_t *locked(pthread_mutex_t *p){pthread_mutex_lock(p);return p;}
static void unlocked(pthread_mutex_t **p){pthread_mutex_unlock(*p);}
#define guard(x) pthread_mutex_t *CAT(lock_,__LINE__) __attribute__((cleanup(unlocked)))=locked
static pthread_mutex_t hdcp1_mutex_g=PTHREAD_MUTEX_INITIALIZER,hdcp2_mutex_g=PTHREAD_MUTEX_INITIALIZER;
static bool hdcp_uses_smcinvoke;
struct hdcp1_qsee_handle {char payload[80];u32 max_hdcp_key_verify_retries;};
struct hdcp2_qsee_handle {char payload[160];u32 max_hdcp_key_verify_retries;};
/* FUNCTIONS */
int main(void) {
 unsigned cases=0;u8 bytes[5];u32 state=7;for(unsigned i=0;i<100000;i++){for(unsigned j=0;j<5;j++){state=state*1664525U+1013904223U;bytes[j]=state>>24;}int ones=0;for(unsigned j=0;j<5;j++)ones+=__builtin_popcount(bytes[j]);assert(hdcp1_count_ones(bytes,5)==ones);u32 low;memcpy(&low,bytes,4);assert((hdcp1_validate_aksv(bytes[4],low)==0)==(ones==20));cases++;}
 hdcp_uses_smcinvoke=true;void *ctx=malloc(1);assert(ctx);hdcp1_set_hdcp_key_verify_retries(ctx,55);hdcp2_set_hdcp_key_verify_retries(ctx,55);free(ctx);
 hdcp1_set_hdcp_key_verify_retries(NULL,55);hdcp2_set_hdcp_key_verify_retries(NULL,55);
 hdcp_uses_smcinvoke=false;struct hdcp1_qsee_handle one={0};struct hdcp2_qsee_handle two={0};hdcp1_set_hdcp_key_verify_retries(&one,17);hdcp2_set_hdcp_key_verify_retries(&two,23);assert(one.max_hdcp_key_verify_retries==17&&two.max_hdcp_key_verify_retries==23);
 printf("{\"aksv_cases\":%u,\"smcinvoke_handle_not_cast_to_qseecom\":true,\"legacy_retry_policy\":true}\n",cases);
}
