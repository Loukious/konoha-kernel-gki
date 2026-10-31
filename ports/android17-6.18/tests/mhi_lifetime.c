#include "mock.h"
#define MHI_DEVICE_CONTROLLER 0
#define MHI_DEVICE_XFER 1
enum mhi_ee_type{MHI_EE_AMSS=2,MHI_EE_MAX=8};struct device{void *bus;int refs,deleted;};struct mhi_controller{int x;};struct mhi_chan{struct mhi_device *mhi_dev;u32 ee_mask;};struct mhi_device{struct device dev;struct mhi_controller *mhi_cntrl;struct mhi_chan *ul_chan,*dl_chan;int dev_type;const char *name;};static int mhi_bus_type;
#define to_mhi_device(p) container_of(p,struct mhi_device,dev)
static void device_del(struct device *d){struct mhi_device *m=to_mhi_device(d);assert(!d->deleted);assert((m->ul_chan&&m->ul_chan->mhi_dev==m)||(m->dl_chan&&m->dl_chan->mhi_dev==m));d->deleted=1;}
static void put_device(struct device *d){assert(d->refs>0);d->refs--;if(!d->refs){struct mhi_device *m=to_mhi_device(d);assert(!m->ul_chan&&!m->dl_chan);}}
/* FUNCTIONS */
int main(void){unsigned cases=0;for(int ul=0;ul<2;ul++)for(int dl=0;dl<2;dl++){if(!ul&&!dl)continue;struct mhi_controller c={0};struct mhi_device m={.dev={&mhi_bus_type,2+ul+dl,0},.mhi_cntrl=&c,.dev_type=1};struct mhi_chan u={&m,BIT(2)},d={&m,BIT(3)};m.ul_chan=ul?&u:NULL;m.dl_chan=dl?&d:NULL;enum mhi_ee_type ee=2;assert(!mhi_destroy_device(&m.dev,&ee));bool remove=!dl;assert(m.dev.deleted==remove);if(!remove){assert(m.dev.refs==2+ul+dl);assert(!mhi_destroy_device(&m.dev,NULL));}
assert(m.dev.deleted&&m.dev.refs==1&&!m.ul_chan&&!m.dl_chan);if(ul)assert(!u.mhi_dev);if(dl)assert(!d.mhi_dev);put_device(&m.dev);cases++;}
struct mhi_device m={.dev_type=0,.dev={&mhi_bus_type,1,0}};assert(!mhi_destroy_device(&m.dev,NULL)&&!m.dev.deleted);printf("{\"paired_channel_lifetime_cases\":%u,\"external_refs_outlive_detached_channels\":true,\"environment_mismatch_drops_no_refs\":true}\n",cases);}
