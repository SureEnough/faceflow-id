// edge-box/src/pipeline/pipeline.h
// 单路视频流水线：采集 → 检测/跟踪 → 特征 → 1:N → 客流 → 入库。
#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "backend/inference_backend.h"
#include "common/common.h"
#include "config/config.h"
#include "face/face_engine.h"
#include "flow/flow_counter.h"
#include "recognizer/recognizer.h"
#include "report/report_client.h"
#include "store/recognition_store.h"
#include "tracker/iou_tracker.h"
#include "video/video_source.h"

namespace eb {

// 每秒处理帧数上限（多路时避免占用过多 CPU）
constexpr int kTargetFps = 10;

class Pipeline {
 public:
  Pipeline(CameraConfig cam, int64_t device_id, FaceEngine* face, Recognizer* recognizer,
           RecognitionStore* store, ReportClient* report, float recog_thresh);

  bool Open();
  // 处理一帧：返回是否成功读取
  bool ProcessOneFrame();
  void Run(int max_frames);   // 循环处理；max_frames<=0 无限（Ctrl+C 退出）

  // 返回当前客流统计（值语义：无虚拟线/未启用时为 0 计数，
  // 避免悬挂引用——FlowStats 由成员持有，这里按值返回更安全）
  FlowStats flow() const { return flow_ ? flow_->stats() : FlowStats{}; }
  // 最近一帧快照（Web 预览数据源；线程安全拷贝）
  ImageFrame FrameSnapshot() const;
  // 线程安全运行统计（多线程流水线下由 worker 线程更新，状态板/主线程只读）
  int64_t Frames() const { return frames_.load(); }
  bool LastFrameOk() const { return last_frame_ok_.load(); }
  const std::string& camera_id() const { return cam_.camera_id; }

 private:
  void HandleTrack(const Track& track, const FaceSample& sample);

  CameraConfig cam_;
  int64_t device_id_ = 0;
  FaceEngine* face_;
  Recognizer* recognizer_;
  RecognitionStore* store_;
  ReportClient* report_;
  float recog_thresh_;
  std::unique_ptr<VideoSource> video_;
  IOUTracker tracker_;
  std::unique_ptr<FlowCounter> flow_;
  std::atomic<int64_t> frames_{0};
  std::atomic<bool> last_frame_ok_{true};

  mutable std::mutex frame_mu_;
  ImageFrame last_frame_;
};

}  // namespace eb