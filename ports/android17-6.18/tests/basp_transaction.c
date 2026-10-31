static int prepared[3],commits[3],aborts[3],error_at=-1;
static int prepare(void *p,const struct mca_basp_update *u) {
 int i=*(int *)p;assert(u->ffc_size==1&&u->normal_size==1);
 if(i==error_at)return -ENOMEM;prepared[i]++;return 0;
}
static void commit(void *p) { int i=*(int *)p;assert(prepared[i]);commits[i]++; }
static void abort_update(void *p) { int i=*(int *)p;assert(prepared[i]);aborts[i]++; }
static void reset(void) {memset(prepared,0,sizeof(prepared));memset(commits,0,sizeof(commits));memset(aborts,0,sizeof(aborts));error_at=-1;}
int main(void) {
 int ids[]={0,1,2};int types[]={MCA_SMARTCHG_IF_CHG_TYPE_JEITA,MCA_SMARTCHG_IF_CHG_TYPE_QC,MCA_SMARTCHG_IF_CHG_TYPE_WL_QC};
 struct mca_smartchg_if_ops ops[3]={0};
 struct smart_charge_info info={.enable_fv_dec_by_cc=1};
 struct smart_basp_transport transport={.info=&info};
 struct smart_basp_decoded d={0};
 for(int i=0;i<3;i++) {ops[i].data=&ids[i];ops[i].prepare_baa_para=prepare;ops[i].commit_baa_para=commit;ops[i].abort_baa_para=abort_update;mock_ops[types[i]]=&ops[i];d.update[i].ffc_size=d.update[i].normal_size=1;}
 assert(!smart_basp_dispatch(&transport,&d));for(int i=0;i<3;i++)assert(commits[i]==1&&!aborts[i]);
 for(int missing=0;missing<3;missing++) {
  reset();mock_ops[types[missing]]=NULL;assert(smart_basp_dispatch(&transport,&d)==-ENODEV);mock_ops[types[missing]]=&ops[missing];
  for(int i=0;i<3;i++)assert(!commits[i]&&aborts[i]==(i<missing));
 }
 for(int fail=0;fail<3;fail++) {
  reset();error_at=fail;assert(smart_basp_dispatch(&transport,&d)==-ENOMEM);
  for(int i=0;i<3;i++)assert(!commits[i]&&aborts[i]==(i<fail));
 }
 reset();info.stopping=true;assert(smart_basp_dispatch(&transport,&d)==-ENODEV);
 info.stopping=false;info.enable_fv_dec_by_cc=0;assert(smart_basp_dispatch(&transport,&d)==-ENODEV);
 transport.info=NULL;assert(smart_basp_dispatch(&transport,&d)==-ENODEV);
 transport.info=&info;info.enable_fv_dec_by_cc=1;d.update[2].ffc_size=d.update[2].normal_size=0;mock_ops[types[2]]=NULL;
 assert(!smart_basp_dispatch(&transport,&d));assert(commits[0]==1&&commits[1]==1&&!commits[2]);
 struct smart_basp_transport *t=kcalloc(1,sizeof(*t),0);t->ref.count=2;info.basp=t;t->info=&info;
 struct smart_basp_file *ctx=kcalloc(1,sizeof(*ctx),0);ctx->ref.count=1;ctx->transport=t;ctx->page=calloc(1,sizeof(*ctx->page));pages++;
 struct vm_area_struct vma={.vm_private_data=ctx};smart_basp_vma_open(&vma);smart_basp_vma_open(&vma);
 assert(ctx->ref.count==3&&module_refs==2);smart_basp_detach(&info);assert(!info.basp&&t->ref.count==1&&!t->info);
 kref_put(&ctx->ref,smart_basp_file_free);assert(ctx->ref.count==2&&pages==1);
 smart_basp_vma_close(&vma);assert(ctx->ref.count==1&&pages==1);
 smart_basp_vma_close(&vma);assert(!pages&&!module_refs&&!allocations);
 puts("{\"all_or_abort_transactions\":true,\"missing_provider_cases\":3,\"prepare_failure_cases\":3,\"detached_contexts\":true,\"forked_vma_refs\":true}");return 0;
}
