#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ENODEV 19
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
struct wiphy { int id; };
struct net_device;
struct wireless_dev { struct wiphy *wiphy; struct net_device *netdev; };
struct net_device { struct wireless_dev *ieee80211_ptr; };
struct station_parameters { int id; };
struct station_del_parameters { int id; };
struct station_info { int id; };
struct key_params { int id; };
struct ieee80211_channel { int id; };
enum nl80211_tx_power_setting { TX_AUTOMATIC, TX_FIXED };
static int calls, got_link, got_index;
static struct net_device *expected_dev;
static struct wiphy *expected_wiphy;
static void saw_device(struct wiphy *w, struct net_device *d) { assert(w==expected_wiphy && d==expected_dev); calls++; }
static int wlan_hdd_change_station(struct wiphy *w, struct net_device *d, const u8 *m, struct station_parameters *p) { saw_device(w,d); return -EAGAIN; }
static int wlan_hdd_cfg80211_add_station(struct wiphy *w, struct net_device *d, const u8 *m, struct station_parameters *p) { saw_device(w,d); return -EAGAIN; }
static int wlan_hdd_cfg80211_del_station(struct wiphy *w, struct net_device *d, struct station_del_parameters *p) { saw_device(w,d); return -EAGAIN; }
static int wlan_hdd_cfg80211_dump_station(struct wiphy *w, struct net_device *d, int idx, u8 *m, struct station_info *p) { saw_device(w,d); got_index=idx; return -EAGAIN; }
static int wlan_hdd_cfg80211_get_station(struct wiphy *w, struct net_device *d, const u8 *m, struct station_info *p) { saw_device(w,d); return -EAGAIN; }
static int wlan_hdd_cfg80211_add_key(struct wiphy *w, struct net_device *d, int link, u8 idx, bool pair, const u8 *m, struct key_params *p) { saw_device(w,d); got_link=link; got_index=idx; assert(pair && m && p); return -EAGAIN; }
static int wlan_hdd_cfg80211_get_key(struct wiphy *w, struct net_device *d, int link, u8 idx, bool pair, const u8 *m, void *cookie, void (*cb)(void *, struct key_params *)) { saw_device(w,d); got_link=link; got_index=idx; cb(cookie, NULL); return -EAGAIN; }
static int wlan_hdd_cfg80211_del_key(struct wiphy *w, struct net_device *d, int link, u8 idx, bool pair, const u8 *m) { saw_device(w,d); got_link=link; got_index=idx; return -EAGAIN; }
static int wlan_hdd_set_default_mgmt_key(struct wiphy *w, struct net_device *d, int link, u8 idx) { saw_device(w,d); got_link=link; got_index=idx; return -EAGAIN; }
static int wlan_hdd_cfg80211_set_default_beacon_key(struct wiphy *w, struct net_device *d, int link, u8 idx) { saw_device(w,d); got_link=link; got_index=idx; return -EAGAIN; }
static int wlan_hdd_cfg80211_set_wiphy_params(struct wiphy *w, u32 changed) { assert(w==expected_wiphy && changed==123); calls++; return -EAGAIN; }
static int wlan_hdd_cfg80211_set_chainmask(struct wiphy *w, u32 tx, u32 rx) { assert(w==expected_wiphy && tx==3 && rx==5); calls++; return -EAGAIN; }
static int wlan_hdd_cfg80211_get_chainmask(struct wiphy *w, u32 *tx, u32 *rx) { assert(w==expected_wiphy); *tx=3; *rx=5; calls++; return -EAGAIN; }
static int wlan_hdd_cfg80211_set_txpower(struct wiphy *w, struct wireless_dev *wd, enum nl80211_tx_power_setting type, int mbm) { assert(w==expected_wiphy && wd->netdev==expected_dev && type==TX_FIXED && mbm==1700); calls++; return -EAGAIN; }
static int wlan_hdd_cfg80211_remain_on_channel(struct wiphy *w, struct wireless_dev *wd, struct ieee80211_channel *ch, unsigned duration, u64 *cookie) { saw_device(w,wd->netdev); assert(ch && duration==99); *cookie=12345; return -EAGAIN; }

/* PRODUCTION ADAPTERS */

static void key_callback(void *cookie, struct key_params *params) { (*(int *)cookie)++; }
int main(void)
{
    struct wiphy w={0}, other={1};
    struct wireless_dev wd={.wiphy=&w};
    struct net_device dev={.ieee80211_ptr=&wd};
    struct key_params key={0};
    u8 mac[6]={0};
    expected_dev=&dev; expected_wiphy=&w; wd.netdev=&dev;
    int cases=0;
    for (int link=-1; link<16; link++) {
        assert(hdd_cfg80211_add_key(&w,&wd,link,4,true,mac,&key)==-EAGAIN);
        assert(got_link==link && got_index==4); cases++;
        int callback_calls=0;
        assert(hdd_cfg80211_get_key(&w,&wd,link,2,true,mac,&callback_calls,key_callback)==-EAGAIN);
        assert(got_link==link && got_index==2 && callback_calls==1); cases++;
    }
    int before=calls;
    assert(hdd_cfg80211_add_key(&w,NULL,0,4,true,mac,&key)==-ENODEV);
    wd.netdev=NULL;
    assert(hdd_cfg80211_add_key(&w,&wd,0,4,true,mac,&key)==-ENODEV);
    wd.netdev=&dev; wd.wiphy=&other;
    assert(hdd_cfg80211_add_key(&w,&wd,0,4,true,mac,&key)==-ENODEV);
    wd.wiphy=&w; dev.ieee80211_ptr=NULL;
    assert(hdd_cfg80211_add_key(&w,&wd,0,4,true,mac,&key)==-ENODEV);
    assert(calls==before); dev.ieee80211_ptr=&wd; cases+=4;
    for (int radio=-2; radio<3; radio++) {
        before=calls;
        int ret=hdd_cfg80211_set_wiphy_params(&w,radio,123);
        assert(ret==(radio==-1 ? -EAGAIN : -EOPNOTSUPP));
        assert(calls==before+(radio==-1)); cases++;
    }
    assert(hdd_cfg80211_set_antenna(&w,-1,3,5)==-EAGAIN);
    u32 tx=0,rx=0;
    assert(hdd_cfg80211_get_antenna(&w,-1,&tx,&rx)==-EAGAIN && tx==3 && rx==5);
    assert(hdd_cfg80211_set_tx_power(&w,&wd,-1,TX_FIXED,1700)==-EAGAIN);
    struct ieee80211_channel channel={0}; u64 cookie=0;
    assert(hdd_cfg80211_remain_on_channel(&w,&wd,&channel,99,&cookie,NULL)==-EAGAIN && cookie==12345);
    before=calls;
    assert(hdd_cfg80211_remain_on_channel(&w,&wd,&channel,99,&cookie,mac)==-EOPNOTSUPP);
    assert(calls==before); cases+=5;
    printf("%d request routing, link-ID and unsupported-selection cases passed\n", cases);
    return 0;
}
