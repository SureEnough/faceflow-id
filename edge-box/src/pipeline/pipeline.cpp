#include "pipeline/pipeline.h"

#include <chrono>
#include <thread>

#include "common/image_codec.h"

namespace eb {

Pipeline::Pipeline(CameraConfig cam, int64_t device_id, FaceEngine* face, Recognizer* recognizer,
                   RecognitionStore* store, ReportClient* report, float recog_thresh)
    : cam_(std::move(cam)), device_id_(device_id), face_(face), recognizer_(recognizer), store_(store),
      report_(report), recog_thresh_(recog_thresh) {
  video_ = CreateVideoSource(cam_.url);
}

bool Pipeline::Open() {
  if (!video_->Open()) {
    LOG_ERROR("open video source failed: %s", cam_.url.c_str());
    return false;
  }
  // 每路独立虚拟线（count_flow 时）
  flow_ = std::make_unique<FlowCounter>(cam_.line_x1, cam_.line_y1, cam_.line_x2, cam_.line_y2);
  LOG_INFO("[%s] video opened %dx%d", cam_.camera_id.c_str(), video_->Width(), video_->Height());
  return true;
}

bool Pipeline::ProcessOneFrame() {
  ImageFrame frame;
  if (!video_->Read(frame)) {
    last_frame_ok_.store(false);
    return false;
  }
  last_frame_ok_.store(true);
  frames_.fetch_add(1);

  // 1. 检测 + 采样（得分最高框）
  FaceSample sample;
  if (!face_->Sample(frame, 0.5f, sample)) return true;  // 无人脸跳过

  // 2. 跟踪（此帧只上报一个新出现的轨迹特征，避免重复计次）
  std::vector<FaceBox> boxes{sample.box};
  std::vector<Track> tracks;
  tracker_.Update(boxes, tracks);
  if (tracks.empty()) return true;
  const Track& track = tracks.back();

  // 3. 客流判向（按虚拟线）
  int dir = -1;
  if (cam_.count_flow && flow_) {
    dir = flow_->Update(track.id, track.cx, track.cy);
  }

  // 4. 1:N 识别（区分顾客/内部人员；内部人员不计入顾客客流已在统计口径外）
  MatchResult match;
  if (sample.has_feature) {
    match = recognizer_->Search(sample.feature, recog_thresh_);
    if (match.hit) {
      LOG_INFO("[%s] track=%s -> customer=%lld type=%s sim=%.3f",
               cam_.camera_id.c_str(), track.id.c_str(),
               static_cast<long long>(match.customer_id),
               match.person_type == kStaff ? "STAFF" : "CUSTOMER",
               match.similarity);
    }
  }

  // 5. 入库（仅新轨迹一次，避免同 track 每帧重复；匿名轨迹也保存供历史回查）
  if (track.alive != 1) return true;
  Recognition rec;
  rec.track_id = track.id;
  rec.customer_id = match.hit ? match.customer_id : -1;
  rec.person_type = match.hit ? match.person_type : kCustomer;
  rec.similarity = match.similarity;
  rec.direction = dir;
  rec.camera_id = cam_.camera_id;
  rec.created_at = std::time(nullptr);
  // 新轨迹首帧抓拍对齐人脸图（A2：快照上报；生产走 JPEG，无依赖时 BMP）
  if (track.alive == 1) {
    ImageFrame aligned;
    if (face_->AlignCrop(frame, sample.box, aligned)) {
      std::string b64, mime;
      if (EncodeSnapshotBase64(aligned, b64, mime)) {
        rec.snapshot_b64 = std::move(b64);
        rec.snapshot_mime = std::move(mime);
      }
    }
  }
  store_->Insert(rec);

  if (dir >= 0) {
    LOG_INFO("[%s] flow: %s %s (total in=%lld out=%lld)",
             cam_.camera_id.c_str(), track.id.c_str(),
             dir == 0 ? "IN" : "OUT",
             static_cast<long long>(flow_->stats().in),
             static_cast<long long>(flow_->stats().out));
  }
  return true;
}

void Pipeline::Run(int max_frames) {
  int frames = 0;
  const auto interval = std::chrono::milliseconds(1000 / kTargetFps);
  while (max_frames <= 0 || frames < max_frames) {
    auto begin = std::chrono::steady_clock::now();
    if (!ProcessOneFrame()) {
      LOG_WARN("[%s] read frame failed, retry...", cam_.camera_id.c_str());
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    ++frames;
    // 上报调度：每 report_interval 秒把待上报记录批量送出（简化：每 10 帧）
    if (frames % 10 == 0) {
      auto pending = store_->Pending(200);
      if (!pending.empty()) {
        std::vector<Recognition> recs;
        for (auto& p : pending) {
          recs.push_back(p.rec);
          store_->MarkConfirmed(p.rec);
        }
        report_->Upload(device_id_, recs);
      }
    }
    auto elapsed = std::chrono::steady_clock::now() - begin;
    if (elapsed < interval) {
      std::this_thread::sleep_for(interval - elapsed);
    }
  }
}

}  // namespace eb