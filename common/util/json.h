#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ws {
using json = nlohmann::json;
}  // namespace ws

namespace nlohmann {

template <>
struct adl_serializer<AVRational> {
  static void to_json(json &j, const AVRational &r) {
    j = json{
      {"num", r.num},
      {"den", r.den},
    };
  }

  static void from_json(const json &j, AVRational &r) {
    j.at("num").get_to(r.num);
    j.at("den").get_to(r.den);
  }
};

template <>
struct adl_serializer<AVCodecParameters> {
  static void to_json(json &j, const AVCodecParameters &par) {
    j = json{
      {"codec_type", par.codec_type},
      {"codec_id", par.codec_id},
      {"codec_tag", par.codec_tag},
      {"bit_rate", par.bit_rate},
      {"bits_per_coded_sample", par.bits_per_coded_sample},
      {"bits_per_raw_sample", par.bits_per_raw_sample},
      {"profile", par.profile},
      {"level", par.level},
    };
    switch (par.codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      j["format"] = par.format;
      j["width"] = par.width;
      j["height"] = par.height;
      j["field_order"] = par.field_order;
      j["color_range"] = par.color_range;
      j["color_primaries"] = par.color_primaries;
      j["color_trc"] = par.color_trc;
      j["color_space"] = par.color_space;
      j["chroma_location"] = par.chroma_location;
      j["sample_aspect_ratio"] = par.sample_aspect_ratio;
      j["video_delay"] = par.video_delay;
      break;
    case AVMEDIA_TYPE_AUDIO:
      j["format"] = par.format;
      j["channel_layout"] = par.channel_layout;
      j["channels"] = par.channels;
      j["sample_rate"] = par.sample_rate;
      j["block_align"] = par.block_align;
      j["frame_size"] = par.frame_size;
      // j["delay"] =
      j["initial_padding"] = par.initial_padding;
      j["trailing_padding"] = par.trailing_padding;
      j["seek_preroll"] = par.seek_preroll;
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      j["width"] = par.width;
      j["height"] = par.height;
      break;
    default: break;  // ignore
    }
  }

  static void from_json(const json &j, AVCodecParameters &par) {
    j.at("codec_type").get_to(par.codec_type);
    j.at("codec_id").get_to(par.codec_id);
    j.at("codec_tag").get_to(par.codec_tag);
    j.at("bit_rate").get_to(par.bit_rate);
    j.at("bits_per_coded_sample").get_to(par.bits_per_coded_sample);
    j.at("bits_per_raw_sample").get_to(par.bits_per_raw_sample);
    j.at("profile").get_to(par.profile);
    j.at("level").get_to(par.level);
    switch (par.codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      j.at("format").get_to(par.format);
      j.at("width").get_to(par.width);
      j.at("height").get_to(par.height);
      j.at("field_order").get_to(par.field_order);
      j.at("color_range").get_to(par.color_range);
      j.at("color_primaries").get_to(par.color_primaries);
      j.at("color_trc").get_to(par.color_trc);
      j.at("color_space").get_to(par.color_space);
      j.at("chroma_location").get_to(par.chroma_location);
      j.at("sample_aspect_ratio").get_to(par.sample_aspect_ratio);
      j.at("video_delay").get_to(par.video_delay);
      break;
    case AVMEDIA_TYPE_AUDIO:
      j.at("format").get_to(par.format);
      j.at("channel_layout").get_to(par.channel_layout);
      j.at("channels").get_to(par.channels);
      j.at("sample_rate").get_to(par.sample_rate);
      j.at("block_align").get_to(par.block_align);
      j.at("frame_size").get_to(par.frame_size);
      // j.at("delay").get_to()
      j.at("initial_padding").get_to(par.initial_padding);
      j.at("trailing_padding").get_to(par.trailing_padding);
      j.at("seek_preroll").get_to(par.seek_preroll);
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      j.at("width").get_to(par.width);
      j.at("height").get_to(par.height);
      break;
    default: break;  // ignore
    }
  }
};

}  // namespace nlohmann

#ifndef WS_JSON_STREAM_IGNORE

#include "common/media/stream.h"

namespace ws {

using stream_t = std::shared_ptr<Stream>;
using stream_map_t = std::unordered_map<std::string, stream_t>;

}  // namespace ws

namespace nlohmann {

template <>
struct adl_serializer<ws::stream_map_t> {
  static void to_json(json &j, const ws::stream_map_t &m) {
    for (auto &&e : m) {
      json s;
      s["id"] = e.first;
      for (auto &&f : e.second->GetStreamSubs()) {
        auto type = f.first;
        auto sub = f.second;
        s[av_get_media_type_string(type)] = {
          {"codecpar", *sub.stream->codecpar},
        };
      }
      j.push_back(s);
    }
  }

  static void from_json(const json &j, ws::stream_map_t &m) {
    (void)j;
    (void)m;
  }
};

}  // namespace nlohmann

namespace ws {

inline
std::string to_string(const stream_map_t &m) {
  return json{
    {"streams", m},
  }.dump();
}

}  // namespace ws

#endif  // WS_JSON_STREAM_IGNORE

#ifndef WS_JSON_STREAM_INFO_IGNORE

#include "common/media/stream_def.h"

namespace ws {

using stream_sub_info_t = StreamSubInfo;
using stream_sub_info_p = std::shared_ptr<stream_sub_info_t>;
using stream_info_t = StreamInfo;
using stream_infos_t = std::vector<stream_info_t>;

}  // namespace ws

namespace nlohmann {

template <>
struct adl_serializer<ws::stream_sub_info_p> {
  static void to_json(json &j, const ws::stream_sub_info_p &i) {
    if (i == nullptr) {
      j = nullptr;
    } else {
      j = json{
        {"codecpar", *i->codecpar},
      };
    }
  }

  static void from_json(const json &j, ws::stream_sub_info_p &i) {
    if (j.is_null()) {
      i = nullptr;
    } else {
      j.at("codecpar").get_to(*i->codecpar);
    }
  }
};

template <>
struct adl_serializer<ws::stream_infos_t> {
  static void to_json(json &j, const ws::stream_infos_t &i) {
    (void)j;
    (void)i;
  }

  static void from_json(const json &j, ws::stream_infos_t &i) {
    for (auto &&j_info : j) {
      ws::stream_info_t info;
      j_info.at("id").get_to(info.id);
      for (int t_i = AVMEDIA_TYPE_UNKNOWN,
               t_end = AVMEDIA_TYPE_NB; t_i <= t_end; ++t_i) {
        auto t = static_cast<AVMediaType>(t_i);
        auto t_s = av_get_media_type_string(t);
        if (!t_s) continue;
        if (j_info.contains(t_s)) {
          auto sub_info = std::make_shared<ws::stream_sub_info_t>();
          j_info.at(t_s).get_to(sub_info);
          info.subs[t] = sub_info;
        }
      }
      i.push_back(info);
    }
  }
};

}  // namespace nlohmann

namespace ws {

inline
stream_infos_t to_stream_infos(const std::string &s) {
  auto j = json::parse(s);
  stream_infos_t infos;
  j.at("streams").get_to(infos);
  return infos;
}

}  // namespace ws

#endif  // WS_JSON_STREAM_INFO_IGNORE
