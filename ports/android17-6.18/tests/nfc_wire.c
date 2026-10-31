#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
#define pr_err(...) do {} while(0)
#define pr_debug(...) do {} while(0)
#define pr_info(...) do {} while(0)
#define usleep_range(a,b) do {} while(0)
#define NCI_CMD 0x20
#define NCI_RSP 0x40
#define NCI_NTF 0x60
#define NCI_HDR_LEN 3
#define NCI_PAYLOAD_LEN_IDX 2
#define DL_CMD 0
#define DL_PAYLOAD_BYTE_ZERO 0
#define FW_MSG_CMD_RSP 0
#define MAX_RETRY_COUNT 3
#define NO_RETRY 1
#define NCI_RESET_RESP_READ_DELAY 10000
/* DEFINES */
enum nfc_state_flags { NFC_STATE_UNKNOWN, NFC_STATE_FW_DWL, NFC_STATE_NCI, NFC_STATE_FW_TEARED };
enum chip_types { CHIP_SN1XX=1,CHIP_SN220=2,CHIP_SN300=0xd3,CHIP_UNKNOWN=255 };
struct nfc_dev {
 uint8_t *write_kbuf,*read_kbuf,fw_major_version;
 struct { struct { uint8_t chip_type,rom_version,fw_major,fw_minor; } info; } nqx_info;
 int (*nfc_read)(struct nfc_dev *,char *,size_t,int);
 int (*nfc_write)(struct nfc_dev *,const char *,size_t,int);
 int (*nfc_enable_intr)(struct nfc_dev *);
 int (*nfc_disable_intr)(struct nfc_dev *);
};
static unsigned calls,offs,ons;
static int write_ret,read_ret[2];static unsigned char frames[2][32];
static int read_mock(struct nfc_dev *d,char *p,size_t n,int timeout) { (void)d;assert(timeout==2000 && calls<2);int ret=read_ret[calls];if(ret>0) { assert(ret<=(int)n);memcpy(p,frames[calls],ret); } calls++;return ret; }
static int write_mock(struct nfc_dev *d,const char *p,size_t n,int retries) { (void)d;(void)p;assert(n==4 || n==8);assert(retries==1 || retries==3);return write_ret; }
static int enable_mock(struct nfc_dev *d) { (void)d;ons++;return 0; }
static int disable_mock(struct nfc_dev *d) { (void)d;offs++;return 0; }
/* FUNCTIONS */
int main(void) {
 uint8_t cmd[8],rsp[20];struct nfc_dev d={.write_kbuf=cmd,.read_kbuf=rsp,.nfc_read=read_mock,.nfc_write=write_mock,.nfc_enable_intr=enable_mock,.nfc_disable_intr=disable_mock};
 uint8_t packet[32]={0x60,0,0};unsigned bounds=0;
 for(size_t len=0;len<=32;len++)for(unsigned n=0;n<256;n++) { packet[2]=n;assert(nfc_reset_notification_valid(packet,len)==(len>=3 && n>=4 && n<=len-3));bounds++; }
 assert(!nfc_reset_notification_valid(NULL,32));packet[0]=0x40;assert(!nfc_reset_notification_valid(packet,32));packet[0]=0x60;packet[1]=1;assert(!nfc_reset_notification_valid(packet,32));
 unsigned exchanges=0;
 for(int w=-1;w<=4;w++)for(int r=-1;r<=4;r++)for(int len=-1;len<=13;len++)for(int payload=0;payload<=20;payload++) {
   write_ret=w;read_ret[0]=r;read_ret[1]=len;calls=offs=ons=0;memset(frames,0,sizeof(frames));
   frames[0][0]=0x40;frames[0][2]=1;frames[1][0]=0x60;frames[1][2]=payload;
   if(payload>=4 && payload<=10) { frames[1][3+payload-4]=0xd3;frames[1][3+payload-3]=2;frames[1][3+payload-2]=0x20;frames[1][3+payload-1]=0x42; }
   enum chip_types chip=get_nfcc_chip_type(&d);bool ok=w==4 && r==4 && len>=3 && payload>=4 && payload<=10 && payload<=len-3;
   assert(chip==(ok?CHIP_SN300:CHIP_UNKNOWN));if(w==4)assert(offs==1 && ons==1);exchanges++;
 }
 for(int w=-1;w<=8;w++)for(int r=-1;r<=8;r++) {
   write_ret=w;read_ret[0]=r;calls=0;memset(frames,0,sizeof(frames));
   assert(get_nfcc_session_state_dl(&d)==(w==8 && r==8?NFC_STATE_FW_DWL:NFC_STATE_UNKNOWN));
   frames[0][3]=1;calls=0;assert(get_nfcc_session_state_dl(&d)==(w==8 && r==8?NFC_STATE_FW_TEARED:NFC_STATE_UNKNOWN));
 }
 for(int r=-1;r<=20;r++) { write_ret=8;read_ret[0]=r;calls=0;memset(frames,0,sizeof(frames));frames[0][4]=1;frames[0][7]=1;assert(get_nfcc_chip_type_dl(&d)==(r==20?CHIP_SN220:CHIP_UNKNOWN)); }
 printf("{\"notification_bounds_cases\":%u,\"reset_exchange_cases\":%u,\"session_cases\":200,\"version_cases\":22,\"sanitizers\":[\"AddressSanitizer\",\"UndefinedBehaviorSanitizer\"]}\n",bounds,exchanges);
}
