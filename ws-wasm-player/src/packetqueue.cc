#include "packetqueue.h"
std::shared_ptr<net::Data> PacketQueue::flushPkt;

MyAVPacketList::MyAVPacketList()
    :next{nullptr}
    ,serial{-1}
{

}

MyAVPacketList::~MyAVPacketList()
{

}

PacketQueue::PacketQueue()
    :first_pkt{nullptr}
    ,last_pkt{nullptr}
    ,nb_packets{0}
    ,size{0}
    ,duration{0}
    ,abort_request{0}
    ,serial{-1}
{
}

PacketQueue::~PacketQueue()
{
    flush();
}

void PacketQueue::mustInitOnce()
{
    av_init_packet(flushPkt->packet);
    flushPkt->packet->data = (uint8_t *)flushPkt->packet;
}

bool PacketQueue::compareFlushPkt(std::shared_ptr<net::Data> pkt)
{
    auto v = (pkt->packet->data == flushPkt->packet->data);
    return v;
}

void PacketQueue::init()
{   
    abort_request = 1;
    flush();
}

void PacketQueue::flush()
{
    MyAVPacketList *pkt, *pkt1;
    pkt = nullptr;
    pkt1 = nullptr;
    std::unique_lock<std::mutex> lock(mutex);
    for (pkt = first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(pkt->pkt->packet);
//        av_freep(&pkt); // todo delete or av_freep ??
        delete pkt;
    }
    last_pkt = nullptr;
    first_pkt = nullptr;
    nb_packets = 0;
    size = 0;
    duration = 0;
}

void PacketQueue::abort()
{
    std::unique_lock<std::mutex> lock(mutex);
    abort_request = 1;
    cond.notify_one();
}

void PacketQueue::start()
{
    std::unique_lock<std::mutex> lock(mutex);
    abort_request = 0;
    putPrivate(flushPkt);
}

int PacketQueue::put(std::shared_ptr<net::Data> pkt)
{
    int ret;

    mutex.lock();
    ret = putPrivate(pkt);
    mutex.unlock();

    if (pkt != flushPkt && ret < 0)
        av_packet_unref(pkt->packet);
    return ret;
}

int PacketQueue::putNullPkt(int streamIndex)
{
    std::shared_ptr<net::Data> pkt = std::make_shared<net::Data>();
    AVPacket pkt1;
    pkt->packet = &pkt1;
    av_init_packet(pkt->packet);
    pkt->packet->data = nullptr;
    pkt->packet->size = 0;
    pkt->packet->stream_index = streamIndex;
    return put(pkt);
}

int PacketQueue::putFlushPkt()
{
    return put(flushPkt);
}

int PacketQueue::get(std::shared_ptr<net::Data> pkt, int block, int *serial)
{
    MyAVPacketList *pkt1 = nullptr;
    int ret;

    std::unique_lock<std::mutex> lock(mutex);
    while (1) {
        if (abort_request) {
            ret = -1;
            break;
        }

        pkt1 = first_pkt;
        if (pkt1 != nullptr) {
            first_pkt = pkt1->next;
            if (first_pkt == nullptr)
                last_pkt = nullptr;
            nb_packets--;
            size -= pkt1->pkt->packet->size + int(sizeof(*pkt1));
            duration -= pkt1->pkt->packet->duration;
            pkt = pkt1->pkt;
            if (serial != nullptr) {
                *serial = pkt1->serial;
            }
//          av_free(pkt1);
            delete pkt1;
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            std::unique_lock<std::mutex> lk(mutex);
            cond.wait(lk);
        }
    }    
    return ret;
}

int PacketQueue::putPrivate(std::shared_ptr<net::Data> pkt)
{
    MyAVPacketList *pkt1;

    if (abort_request)
       return -1;

    pkt1 = new MyAVPacketList;
    if (!pkt1)
        return -1;
    pkt1->pkt = pkt;
    pkt1->next = nullptr;
    if (pkt == flushPkt) {
        serial++;
    }
    pkt1->serial = serial;

    if (last_pkt == nullptr)
        first_pkt = pkt1;
    else {
        last_pkt->next = pkt1;
    }
    last_pkt = pkt1;
    nb_packets++;
    size += pkt1->pkt->packet->size + int(sizeof(*pkt1));
    duration += pkt1->pkt->packet->duration;
    /* XXX: should duplicate packet data in DV case */
    cond.notify_one();
    return 0;
}
