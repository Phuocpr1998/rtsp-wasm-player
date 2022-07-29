#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H
#include "common/net/packet.h"

class MyAVPacketList {
public:
    MyAVPacketList();
    ~MyAVPacketList();
public:
    std::shared_ptr<net::Data> pkt;
    MyAVPacketList *next;
    int serial;
};

class PacketQueue {

public:
    static std::shared_ptr<net::Data> flushPkt;
    PacketQueue();
    ~PacketQueue();

    static void mustInitOnce();
    static bool compareFlushPkt(std::shared_ptr<net::Data> pkt);

    void init();

    void flush();
    void abort();
    void start();

    int put(std::shared_ptr<net::Data> pkt);
    int putNullPkt(int streamIndex);
    int putFlushPkt();
    int get(std::shared_ptr<net::Data> pkt, int block, int *serial);

private:
    int putPrivate(std::shared_ptr<net::Data> pkt);
public:
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;

    std::mutex mutex;
    std::condition_variable cond;

};

#endif // PACKETQUEUE_H

