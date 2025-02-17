#pragma once

#include <emscripten/val.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <exception>
#include <chrono>

#include "common/media/stream_video.h"
#include "common/net/json.h"
#include "common/net/packet.h"
#include "common/util/logext.h"

#include "frame.h"

using namespace std::chrono_literals;

class Decoder {
 public:
  using decode_callback_t = std::function<void(std::shared_ptr<Frame>)>;
  using stream_ops_t =
      std::unordered_map<AVMediaType, std::shared_ptr<StreamOp>>;

  class StreamVideoOpContext : public StreamOpContext {
   public:
    explicit StreamVideoOpContext(AVCodecParameters *par)
      : codecpar_(par) {
    }
    ~StreamVideoOpContext() override {}

    AVCodecID GetAVCodecID() override {
      return codecpar_->codec_id;
    }
    void InitAVCodecContext(AVCodecContext *codec_ctx) override {
      int ret = avcodec_parameters_to_context(codec_ctx, codecpar_);
      if (ret < 0) throw StreamError(ret);
    }

   private:
    AVCodecParameters *codecpar_;
  };

  Decoder() {
    VLOG(2) << __func__;
    // ThreadRenderStart();
  }
  ~Decoder() {
    VLOG(2) << __func__;
    ThreadDecodeStop();
    // ThreadRenderStop();
  }

  void Open(
      const std::string &stream_info,
      int queue_size, int thread_count, int thread_type,
      emscripten::val lambda) {
    av_log_set_level(AV_LOG_QUIET);
    VLOG(1) << __func__ << ": " << stream_info;
    stream_info_ = net::to_stream_info(stream_info);
    if (queue_size >= 2) {
      decode_datas_maxsize_ = queue_size;
    }
    if (!lambda.isNull()) {
      decode_cb_ = [lambda](std::shared_ptr<Frame> f) {
        lambda(f);
      };
    } else {
      decode_cb_ = nullptr;
    }

    for (auto &&e : stream_info_.subs) {
      auto type = e.first;
      auto sub_info = e.second;
      switch (type) {
      case AVMEDIA_TYPE_VIDEO: {
        StreamVideoOptions options{};
        options.dec_name = "";
        options.dec_thread_count = thread_count;
        options.dec_thread_type = thread_type;
        options.sws_enable = true;
        if (sub_info->codecpar->format == AV_PIX_FMT_YUVJ420P || (
            sub_info->codecpar->format == AV_PIX_FMT_YUV420P &&
            sub_info->codecpar->color_range == AVCOL_RANGE_JPEG)) {
          options.sws_dst_pix_fmt = AV_PIX_FMT_YUVJ420P;
        } else {
          options.sws_dst_pix_fmt = AV_PIX_FMT_YUV420P;
        }
        stream_ops_[type] = std::make_shared<StreamVideoOp>(
            options,
            std::make_shared<Decoder::StreamVideoOpContext>(
                sub_info->codecpar));
      } break;
      default:
        LOG(WARNING) << "Stream[" << stream_info_.id << "] "
            << "media type not support at present, type="
            << av_get_media_type_string(type);
        break;
      }
    }
  }

  // embind doesn't support pointers to primitive types
  //  https://stackoverflow.com/a/27364643
  std::shared_ptr<Frame> Decode(uintptr_t buf_p, int buf_size) {
    const uint8_t *buf = reinterpret_cast<uint8_t *>(buf_p);
    net::Data data;
    data.FromBytes(buf, buf_size);
    VLOG(1) << "decode packet type=" << av_get_media_type_string(data.type)
        << ", size=" << data.packet->size;

    if (!decode_from_key_frame_) {
      if ((data.packet->flags & AV_PKT_FLAG_KEY) == 0) {
        VLOG(1) << "decode packet ignored, wait key for the first one";
        return nullptr;
      }
      decode_from_key_frame_ = true;
    }

    try {
      auto op = stream_ops_[data.type];
      time_stat_->Beg();
      auto frame = op->GetFrame(data.packet);
      if (frame == nullptr) {
        LOG(WARNING) << "decode frame is null, need new packets";
        return nullptr;
      }
      time_stat_->End();
      VLOG(2) << time_stat_->Log();
      auto f = std::make_shared<Frame>()->Alloc(data.type, frame);
      if (decode_cb_) decode_cb_(f);
      return f;
    } catch (const StreamError &err) {
      LOG(ERROR) << err.what();
      return nullptr;
    }
  }

  // not works well as clone frame not supported in frame.h
  //  otherwise alloc more frames for decoding from packets when op->GetFrame()
  void DecodeAsync(uintptr_t buf_p, int buf_size) {
    ThreadDecodeStart();
    const uint8_t *buf = reinterpret_cast<uint8_t *>(buf_p);
    {
      std::lock_guard<std::mutex> _(decode_mutex_);
      auto data = std::make_shared<net::Data>();
      data->FromBytes(buf, buf_size);
      if (!decode_from_key_frame_) {
        if ((data->packet->flags & AV_PKT_FLAG_KEY) == 0) {
          VLOG(1) << "decode packet ignored, wait key for the first one";
          return;
        }
        decode_from_key_frame_ = true;
      }
      decode_datas_.push_back(data);
      if (decode_datas_.size() > decode_datas_maxsize_) {
        for (auto it = decode_datas_.begin(); it != decode_datas_.end(); ++it) {
          if ((*it)->packet->flags & AV_PKT_FLAG_KEY) {  // key, not erase
            continue;
          }
          it = decode_datas_.erase(it);
          LOG(WARNING)
              << "decode eldest data (not key packet) ignored, as queue size > "
              << decode_datas_maxsize_;
          throw "decode eldest data (not key packet) ignored";
          break;
        }
      }
    }
    decode_cond_.notify_one();
  }

  void GetRenderFrame() {
    if (!decode_results_.empty()) {
      std::lock_guard<std::mutex> lock(decode_results_mutex_);
      auto f = decode_results_.front();
      if (decode_cb_) decode_cb_(f);
      f->Free();
      decode_results_.erase(decode_results_.begin());
    }
  }

 private:
  void ThreadDecodeStart() {
    if (!decode_stop_) return;
    decode_stop_ = false;
    decode_thread_ = std::thread(&Decoder::ThreadDecodeRun, this);
  }

  void ThreadDecodeStop() {
    if (decode_stop_) return;
    decode_stop_ = true;
    {
      std::lock_guard<std::mutex> _(decode_mutex_);
      decode_datas_.push_back(nullptr);
    }
    LOG(WARNING) << "Stopping ThreadDecodeRun";
    decode_cond_.notify_all();
    if (decode_thread_.joinable()) {
      decode_thread_.join();
    }
    LOG(WARNING) << "Stopped ThreadDecodeRun";
  }

  void ThreadDecodeRun() {
    while (!decode_stop_) {
      // LOG(WARNING) << "ThreadDecodeRun";
      std::vector<std::shared_ptr<net::Data>> datas;
      {
        std::unique_lock<std::mutex> lock(decode_mutex_);
        decode_cond_.wait_for(lock, 100ms, [this] { return !decode_datas_.empty(); });
        datas = std::move(decode_datas_);
      }
      if (decode_stop_) break;

      for (auto &&data : datas) {
        if (decode_stop_) break;
        VLOG(1) << "decode packet type="
            << av_get_media_type_string(data->type)
            << ", size=" << data->packet->size;
        auto op = stream_ops_[data->type];
        try {
          time_stat_->Beg();
          // note: return frame alloced once here
          auto frame = op->GetFrame(data->packet);
          if (frame == nullptr) {
            LOG(WARNING) << "decode frame is null, need new packets";
            break;
          }
          time_stat_->End();
          VLOG(2) << time_stat_->Log();
          {
            std::lock_guard<std::mutex> lock(decode_results_mutex_);
            decode_results_.push_back(
                std::make_shared<Frame>()->Alloc(data->type, frame));
          }
        } catch (const StreamError &err) {
          // LOG(WARNING) << "GetFrame error" << err.what();
          break;
        }
      }
    }
    LOG(WARNING) << "Stopped ThreadDecodeRun";
  }

  void ThreadRenderStart() {
    if (!render_stop_) return;
    render_stop_ = false;
    render_thread_ = std::thread(&Decoder::ThreadRenderRun, this, decode_cb_);
  }

  void ThreadRenderStop() {
    if (render_stop_) return;
    render_stop_ = true;
    LOG(WARNING) << "Stopping ThreadRenderRun";
    if (render_thread_.joinable()) {
      render_thread_.join();
    }
    LOG(WARNING) << "Stopped ThreadRenderRun";
  }

  void ThreadRenderRun(decode_callback_t decode_cb) {
    while (!render_stop_) {
      // LOG(WARNING) << "ThreadRenderRun";
      std::vector<std::shared_ptr<Frame>> datas;
      {
        std::unique_lock<std::mutex> lock(decode_results_mutex_);
        decode_cond_.wait_for(lock, 1000ms, [this] { return !decode_results_.empty(); });
        datas = std::move(decode_results_);
      }
      if (decode_stop_ || render_stop_) break;

      for (auto &&f : datas) {
        try {
          {
            std::lock_guard<std::mutex> lock(render_frames_mutex_);
            render_frames_.push_back(f);
          }
        } catch(std::exception &e) {
          LOG(ERROR) << "ThreadRenderRun" << e.what();
        }
      }
      std::this_thread::sleep_for(10ms);
    }
    LOG(WARNING) << "Stopped ThreadRenderRun";
  }

  StreamInfo stream_info_;
  stream_ops_t stream_ops_;
  decode_callback_t decode_cb_;
  bool decode_from_key_frame_{false};

  std::atomic_bool decode_stop_{true};
  std::size_t decode_datas_maxsize_{2};
  std::vector<std::shared_ptr<net::Data>> decode_datas_;
  std::thread decode_thread_;
  std::mutex decode_mutex_;
  std::condition_variable decode_cond_;

  std::atomic_bool render_stop_{true};
  std::thread render_thread_;

  std::mutex decode_results_mutex_;
  std::vector<std::shared_ptr<Frame>> decode_results_;

  std::mutex render_frames_mutex_;
  std::vector<std::shared_ptr<Frame>> render_frames_;

  std::shared_ptr<logext::TimeStat> time_stat_ = logext::TimeStat::Create(60*2);

  double frame_timer, max_frame_duration = 10.0, remaining_time = 0.0;
};
