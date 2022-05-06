struct k_work global_l2cap_scheduler_work;


void l2cap_sched_set_pending() {
    k_work_poll_submit(global_l2cap_scheduler_work, /* Block on conn.c:free_tx */);
}

void global_l2cap_scheduler_work() {
    int had_work = 0;
    foreach(chan in all connections) {
        had_work |= l2cap_chan_tx_next_fragment(chan);
    }
    if (had_work) {
        l2cap_sched_set_pending();
    }
}
