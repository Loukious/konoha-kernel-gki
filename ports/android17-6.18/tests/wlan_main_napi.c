static void clean(void)
{
    assert(objects == 0 && devices == 0 && attached == 0);
    assert(enabled == 0 && hooks == 0 && locks == 0);
}

int main(void)
{
    struct CE_state ce[3] = {{.htt_rx_data=true}, {.htt_rx_data=true}, {.htt_rx_data=true}};
    struct qdf_device qdev = {0};
    int cases = 0;

    /* Every struct/device allocation fails in turn before NAPI publication. */
    for (int failure = 0; failure <= 9; failure++) {
        struct hif_softc hif = {.ce_count=3, .qdf_dev=&qdev};
        for (int i=0; i<3; i++) hif.ce_id_to_state[i]=&ce[i];
        alloc_calls=0; fail_at=failure;
        int ret=hif_napi_create((void *)&hif, hif_exec_poll, 64, 2, 0);
        if (failure) {
            assert(ret == -ENOMEM && hif.napi_data.state == 0);
        } else {
            assert(ret == 7 && devices == 6 && attached == 6);
            assert(hif_napi_create((void *)&hif, hif_exec_poll, 64, 2, 0) == -EALREADY);
            for (int i=0; i<3; i++) qdf_napi_enable(&hif.napi_data.napis[i]->napi);
            hif.napi_data.state |= HIF_NAPI_CONF_UP;
            assert(hif_napi_destroy((void *)&hif, 1, 0) == -EPERM);
            assert(devices == 6 && enabled == 6);
            for (int i=0; i<3; i++) assert(hif_napi_destroy((void *)&hif, i+1, 1) == 0);
        }
        clean(); cases++;
    }
    {
        struct hif_softc hif = {.ce_count=3, .qdf_dev=&qdev};
        for (int i=0; i<3; i++) hif.ce_id_to_state[i]=&ce[i];
        alloc_calls=0; fail_at=0; cpu_error=-EIO;
        assert(hif_napi_create((void *)&hif, hif_exec_poll, 64, 2, 0) == -EIO);
        cpu_error=0; clean(); cases++;
    }
    for (int failure=0; failure<=2; failure++) {
        alloc_calls=0; fail_at=failure;
        struct hif_exec_context *ctx=hif_exec_napi_create(2);
        if (failure) assert(!ctx);
        else {
            assert(ctx && devices == 1 && attached == 1 && enabled == 1);
            hif_exec_napi_kill(ctx);
            hif_exec_napi_kill(ctx);
            qdf_mem_free(ctx);
        }
        clean(); cases++;
    }
    for (int failure=0; failure<=1; failure++) {
        struct dp_rx_thread thread={0};
        alloc_calls=0; fail_at=failure;
        int ret=dp_rx_tm_thread_napi_init(&thread);
        assert(ret == (failure ? QDF_STATUS_E_NOMEM : QDF_STATUS_SUCCESS));
        dp_rx_tm_thread_napi_deinit(&thread);
        dp_rx_tm_thread_napi_deinit(&thread);
        clean(); cases++;
    }
    for (int state=0; state<2; state++) {
        struct dp_rx_thread started={.task=&started, .shutdown_event=1};
        struct dp_rx_thread unstarted={0};
        struct dp_rx_thread *threads[]={&started, &unstarted, NULL};
        struct dp_rx_tm_handle handle={.num_dp_rx_threads=3, .state=state, .rx_thread=threads};
        thread_waits=thread_wakes=0;
        assert(dp_rx_tm_shutdown(&handle) == QDF_STATUS_SUCCESS);
        assert(thread_waits == 1 && thread_wakes == 1);
        assert(started.event_flag == 3 && !unstarted.event_flag);
        assert(!started.task && !unstarted.task && !handle.state);
        assert(dp_rx_tm_shutdown(&handle) == QDF_STATUS_SUCCESS);
        assert(thread_waits == 1 && thread_wakes == 1);
        clean(); cases++;
    }
    printf("%d NAPI allocation/unwind/active-removal and partial-thread cases passed\n", cases);
    return 0;
}
