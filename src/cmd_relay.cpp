#include "RelayServer.h"



void cmd_relay(const std::vector<std::string> &subArgs) {
    RelayServer s;
    s.run();
}

void RelayServer::run() {
    tpWebsocket.init("Websocket", 1, [this](auto &thr){
        runWebsocket(thr);
    });

    tpIngester.init("Ingester", cfg().relay__numThreads__ingester, [this](auto &thr){
        runIngester(thr);
    });

    tpWriter.init("Writer", 1, [this](auto &thr){
        runWriter(thr);
    });

    tpReqWorker.init("ReqWorker", cfg().relay__numThreads__reqWorker, [this](auto &thr){
        runReqWorker(thr);
    });

    tpReqMonitor.init("ReqMonitor", cfg().relay__numThreads__reqMonitor, [this](auto &thr){
        runReqMonitor(thr);
    });

    tpYesstr.init("Yesstr", cfg().relay__numThreads__yesstr, [this](auto &thr){
        runYesstr(thr);
    });

    // Monitor for config file reloads

    auto configFileChangeWatcher = hoytech::file_change_monitor(configFile);

    configFileChangeWatcher.setDebounce(100);

    configFileChangeWatcher.run([&](){
        loadConfig(configFile);
    });

    // Cron

    cron.repeat(10 * 1'000'000UL, [&]{
        cleanupOldEvents();
    });

    cron.setupCb = []{ setThreadName("cron"); };

    cron.run();

    tpWebsocket.join();
}
