#include "RelayServer.h"

#include "ActiveMonitors.h"



void RelayServer::runReqMonitor(ThreadPool<MsgReqMonitor>::Thread &thr) {
    auto dbChangeWatcher = hoytech::file_change_monitor(dbDir + "/data.mdb");

    dbChangeWatcher.setDebounce(100);

    dbChangeWatcher.run([&](){
        tpReqMonitor.dispatchToAll([]{ return MsgReqMonitor{MsgReqMonitor::DBChange{}}; });
    });


    ActiveMonitors monitors;
    uint64_t currEventId = MAX_U64;

    while (1) {
        auto newMsgs = thr.inbox.pop_all();

        auto txn = env.txn_ro();

        uint64_t latestEventId = getMostRecentEventId(txn);
        if (currEventId > latestEventId) currEventId = latestEventId;

        for (auto &newMsg : newMsgs) {
            if (auto msg = std::get_if<MsgReqMonitor::NewSub>(&newMsg.msg)) {
                env.foreach_Event(txn, [&](auto &ev){
                    if (msg->sub.filterGroup.doesMatch(ev.flat_nested())) {
                        sendEvent(msg->sub.connId, msg->sub.subId, getEventJson(txn, ev.primaryKeyId));
                    }

                    return true;
                }, false, msg->sub.latestEventId + 1);

                msg->sub.latestEventId = latestEventId;

                monitors.addSub(txn, std::move(msg->sub), latestEventId);
            } else if (auto msg = std::get_if<MsgReqMonitor::RemoveSub>(&newMsg.msg)) {
                monitors.removeSub(msg->connId, msg->subId);
            } else if (auto msg = std::get_if<MsgReqMonitor::CloseConn>(&newMsg.msg)) {
                monitors.closeConn(msg->connId);
            } else if (std::get_if<MsgReqMonitor::DBChange>(&newMsg.msg)) {
                env.foreach_Event(txn, [&](auto &ev){
                    monitors.process(txn, ev, [&](RecipientList &&recipients, uint64_t quadId){
                        sendEventToBatch(std::move(recipients), std::string(getEventJson(txn, quadId)));
                    });
                    return true;
                }, false, currEventId + 1);

                currEventId = latestEventId;
            }
        }
    }
}
