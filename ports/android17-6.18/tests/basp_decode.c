static void checksum(u8 *b) {
 u32 sum=0,n=get_unaligned_le32(b+8);for(u32 i=4;i<n;i++)sum+=b[i];put32(b,sum);
}
static size_t fixture(u8 *b,u32 steps) {
 memset(b,0,4096);
 put32(b+12,1);put32(b+16,1);
 for(int i=2;i<6;i++)put32(b+12+i*4,1);
 size_t off=36;
 for(int i=0;i<2;i++) {
  put32(b+off,2);put32(b+off+4,15);put32(b+off+8,35);
  put32(b+off+12,4570-i*70);put32(b+off+16,1447);off+=20;
 }
 for(int i=2;i<6;i++) {
  put32(b+off,i<4);put32(b+off+4,!(i%2));put32(b+off+8,1);
  put32(b+off+12,10);put32(b+off+16,48);put32(b+off+20,steps);off+=24;
  for(u32 j=0;j<steps;j++) {
   put32(b+off,4000+j*50);put32(b+off+4,12000-j*1000);put32(b+off+8,1000);off+=12;
  }
 }
 put32(b+8,off);checksum(b);return off;
}
int main(void) {
 u8 b[4096],valid[4096];struct smart_basp_decoded d;unsigned cases=0;
 for(u32 steps=1;steps<=8;steps++) {
  size_t n=fixture(b,steps);assert(!smart_basp_decode(b,n,&d));cases++;
  assert(d.update[0].ffc_size==1&&d.update[0].normal_size==1);
  assert(d.update[0].jeita[0].vterm==4570&&d.update[0].jeita[1].vterm==4500);
  for(int i=1;i<3;i++)for(int j=0;j<2;j++) {
   const struct smart_batt_spec *p=&d.update[i].spec[j];
   assert(p->type==(i==1)&&p->ffc==!j&&p->step_size==steps);
   assert(p->steps[steps-1].mv==4000+(steps-1)*50);
   assert(p->steps[0].ma_h==12000&&p->steps[0].ma_l==1000);
  }
  smart_basp_free_decoded(&d);assert(!allocations);
  for(size_t k=0;k<n;k++) { assert(smart_basp_decode(b,k,&d));cases++;assert(!allocations); }
  memcpy(valid,b,4096);
  for(size_t k=0;k<n;k++) {
   memcpy(b,valid,4096);b[k]^=0x80;assert(smart_basp_decode(b,n,&d));cases++;assert(!allocations);
  }
  for(size_t k=4;k<n;k+=4) {
   memcpy(b,valid,4096);put32(b+k,0xffffffff);
   if(k!=8)checksum(b);
   int ret=smart_basp_decode(b,n,&d);if(!ret)smart_basp_free_decoded(&d);cases++;assert(!allocations);
  }
  for(int k=1;k<=3;k++) {
   attempt=0;fail_at=k;assert(smart_basp_decode(valid,n,&d)==-ENOMEM);
   fail_at=0;assert(!allocations);cases++;
  }
 }
 memset(b,0,4096);put32(b+8,36);checksum(b);assert(!smart_basp_decode(b,36,&d));smart_basp_free_decoded(&d);
 put32(b+8,40);checksum(b);assert(smart_basp_decode(b,40,&d));
 uint32_t rng=0x4baacafe;
 for(int k=0;k<150000;k++) {
  for(int j=0;j<4096;j++) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;b[j]=rng;}
  if(k%2) {put32(b+4,0);put32(b+8,36+(rng%1016)*4);checksum(b);}
  int ret=smart_basp_decode(b,4096,&d);if(!ret)smart_basp_free_decoded(&d);assert(!allocations);cases++;
 }
 printf("{\"decode_cases\":%u,\"allocation_failures\":24,\"wire_order_verified\":true,\"leaks\":0}\n",cases);
 return 0;
}
