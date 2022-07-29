#include "framequeue.h"
#include "packetqueue.h"
#include "frame.h"

FrameQueue::FrameQueue(int64_t frameQueueSize)
{
//    this->def = frameQueueSize;
    this->frameQueueSize = frameQueueSize;
    waitForRead = false;
    rindex = 0;
    windex = 0;
    size = 0;
    max_size = frameQueueSize;
    keep_last = 0;
    rindex_shown = 0;
    pktq = nullptr;
    waitForRead = false;
    speed = 1.0;
    queue = new Frame[this->frameQueueSize];
    for (auto i = 0; i < this->frameQueueSize; i++) {
        queue[i].frame_ = nullptr;
        queue[i].speed = &speed;
    }
    hasInit = false;
}

FrameQueue::~FrameQueue()
{
    int i;
    for (i = 0; i < max_size; i++) {
        Frame *vp = &queue[i];        
        unrefItem(vp);
        av_frame_free(&vp->frame_);
        vp->frame_ = nullptr;
    }
    delete[] queue;
}

int FrameQueue::init(PacketQueue *pktq, int maxSize, int keepLast)
{
    // fix later
    // only call init once
    if (hasInit) {
        return 0 ;
    }
    hasInit = true;
    this->pktq = pktq;
//    for (auto i = 0; i < max_size; i++) {
//        Frame *vp = &queue[i];
//        unrefItem(vp);
//        if (vp->frame_ != nullptr) {
//            av_frame_free(&vp->frame_);
//        }
//    }
    this->max_size = FFMIN(maxSize, this->frameQueueSize);
    this->keep_last = !!keepLast;
    for (auto i = 0; i < max_size; i++) {
        if (!(queue[i].frame_ = av_frame_alloc())) {
            // not enough mem for alloc
            return AVERROR(ENOMEM);
        }
    }
    size = rindex_shown = 0;
    rindex = 0;
    windex = 0;
    return 0;
}

void FrameQueue::unrefItem(Frame *vp)
{
    if (vp->frame_ != nullptr) {
        av_frame_unref(vp->frame_);
    }
//    avsubtitle_free(&vp->sub);
}

void FrameQueue::wakeSignal()
{
    std::unique_lock<std::mutex> lock(mutex);
    waitForRead = false;
    cond.notify_one();
}

Frame *FrameQueue::peek()
{
    auto f = &queue[(rindex + rindex_shown) % max_size];
    return f;
}

Frame *FrameQueue::peekNext()
{
    auto f = &queue[(rindex + rindex_shown + 1) % max_size];
    return f;
}

Frame *FrameQueue::peekLast()
{
    return &queue[rindex];
}

Frame *FrameQueue::peekWriteable()
{
    mutex.lock();
    if  (size >= max_size &&
           !pktq->abort_request) {
//        mutex.unlock();
        std::unique_lock<std::mutex> lk(mutex);
        cond.wait(lk);
//        return nullptr;
    }
    mutex.unlock();

    if (pktq->abort_request)
        return nullptr;

    return &queue[windex];
}

Frame *FrameQueue::peekReadable()
{
    /* wait until we have a readable a new frame */
    mutex.lock();
    while (size - rindex_shown <= 0 &&
          !pktq->abort_request) {
       std::unique_lock<std::mutex> lk(mutex);
       cond.wait(lk);
    }
    mutex.unlock();

    if (pktq->abort_request)
       return nullptr;

    return &queue[(rindex + rindex_shown) % max_size];
}

void FrameQueue::queuePush()
{
    if (++windex == max_size){
        windex = 0;
    }
    std::unique_lock<std::mutex> lock(mutex);
    size++;
    cond.notify_one();
    waitForRead = false;
}

void FrameQueue::queueNext()
{
//    qDebug() << "queue Next";
    if (keep_last && !rindex_shown) {
        rindex_shown = 1;
        return;
    }
    unrefItem(&queue[rindex]);
    if (++rindex == max_size)
        rindex = 0;
    std::unique_lock<std::mutex> lock(mutex);
    size--;
    cond.notify_one();
}

int FrameQueue::queueNbRemain()
{
    return size - rindex_shown;
}

int64_t FrameQueue::queueLastPos()
{
    Frame *fp = &queue[rindex];
    if (rindex_shown && fp->serial == pktq->serial)
        return fp->pos;
    else
        return -1;
}

double FrameQueue::bufferPercent()
{
    return (queueNbRemain() /this->frameQueueSize) * 100;
}

bool FrameQueue::isWriteable()
{
    auto allowWrite = !(size >= max_size);
    return allowWrite;
}

void FrameQueue::reset()
{
//    waitForRead = false;
//    rindex = 0;
//    windex = 0;
//    size = 0;
//    keep_last = 0;
//    rindex_shown = 0;
////    pktq = nullptr;
//    waitForRead = false;
//    speed = 1.0;
    auto s = FFMAX(this->max_size, this->frameQueueSize);
    init(this->pktq, s, keep_last);
}
