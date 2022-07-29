#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}
#include <condition_variable>
#include <mutex>

#include "define.h"

class PacketQueue;
class Frame;

class FrameQueue {
public:
    FrameQueue(int64_t frameQueueSize);
    ~FrameQueue();

    int init(PacketQueue *pktq, int maxSize, int keepLast);
    void unrefItem(Frame *vp);
    void wakeSignal();

    Frame *peek();
    Frame *peekNext();
    Frame *peekLast();
    Frame *peekWriteable();
    Frame *peekReadable();
    void queuePush();
    void queueNext();
    int queueNbRemain();
    int64_t queueLastPos();

    double bufferPercent();

    bool isWriteable();

    // reset frame queue
    void reset();

//    void syncAllFrameToNewPts(const double& oldSpeed, const double &newSpeed);

public:
    Frame *queue;
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    std::mutex mutex;
    std::condition_variable cond;
    PacketQueue *pktq;

    bool waitForRead;

    double speed;

private:
//    Define *def;
    int64_t frameQueueSize;
    int hasInit;
//    bool
};
#endif // FRAMEQUEUE_H

