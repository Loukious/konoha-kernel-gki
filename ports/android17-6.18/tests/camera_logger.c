static void clean(void){assert(!alloc_live&&!chrdev_live&&!class_live&&!device_live);}
static void *writer(void *arg){loff_t pos=0;for(int i=0;i<500;i++)assert(cameralog_write(NULL,"race!",5,&pos)==5);return NULL;}
static void *reader(void *arg){loff_t pos=0;char buf[MESSAGE_MAX];for(int i=0;i<500;i++){ssize_t n=cameralog_read(NULL,buf,sizeof(buf),&pos);assert(n==0||n==5);}return NULL;}
int main(void){
 for(int n=1;n<=4;n++){steps=0;fail_step=n;assert(cameralog_init()<0);assert(!camlog_ready);clean();}
 steps=fail_step=0;assert(!cameralog_init());loff_t pos=0;char out[MESSAGE_MAX+1]={0};int bytes=0;
 assert(cameralog_write(NULL,"hello!",6,&pos)==6);assert(s_write_buf.writer_uid==1000&&s_write_buf.writer_pid==10);assert(cameralog_read(NULL,out,0,&pos)==0);assert(cameralog_read(NULL,out,2,&pos)==-EMSGSIZE);assert(kfifo_len(&message_kfifo)==sizeof(struct camlog_dev));fail_copy=true;assert(cameralog_read(NULL,out,100,&pos)==-EFAULT);assert(cameralog_write(NULL,"bad!",4,&pos)==-EFAULT);fail_copy=false;assert(cameralog_read(NULL,out,100,&pos)==6&&!memcmp(out,"hello\n",6));
 assert(cameralog_write(NULL,"long message!",13,&pos)==13);assert(cameralog_write(NULL,"x!",2,&pos)==2);memset(out,0xcc,sizeof(out));assert(!cameralog_ioctl(NULL,IOCTL_CMD_READ_LAST_MSG,(unsigned long)out));assert(!strcmp(out,"x!"));assert(!cameralog_ioctl(NULL,IOCTL_CMD_READ_KFIFO_CNT,(unsigned long)&bytes)&&bytes==2*sizeof(struct camlog_dev));assert(!cameralog_ioctl(NULL,IOCTL_CMD_DUMP,0));assert(!kfifo_len(&message_kfifo));
 char in[2050];memset(in,'x',sizeof(in));in[2049]=0;int cases=0;
 for(size_t n=0;n<2050;n++){ssize_t expected=n<1023?n:1023;assert(cameralog_write(NULL,in,n,&pos)==expected);if(n){assert(cameralog_read(NULL,out,expected-1,&pos)==(expected==1?0:-EMSGSIZE));assert(cameralog_read(NULL,out,expected,&pos)==expected);assert(out[expected-1]=='\n');}cases++;}
 for(int i=0;i<20;i++){char msg[12];int n=snprintf(msg,sizeof(msg),"%02d!",i);assert(cameralog_write(NULL,msg,n,&pos)==n);}assert(kfifo_len(&message_kfifo)==7*sizeof(struct camlog_dev));for(int i=13;i<20;i++){memset(out,0,sizeof(out));assert(cameralog_read(NULL,out,sizeof(out),&pos)==3);assert(atoi(out)==i);}assert(!kfifo_len(&message_kfifo));
 pthread_t a,b;assert(!pthread_create(&a,NULL,writer,NULL));assert(!pthread_create(&b,NULL,reader,NULL));assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));cameralog_exit();assert(cameralog_open(NULL,NULL)==-ENODEV);clean();printf("{\"init_faults\":4,\"message_lengths\":%d,\"copy_fault_retention\":true,\"complete_record_eviction\":true,\"concurrent_io\":1000}\n",cases);
}
