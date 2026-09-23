// 与 C++ WebServer /api 契约对应的类型（snake_case）
export interface VirtualLine {
  x1: number
  y1: number
  x2: number
  y2: number
}

export interface CameraCfg {
  camera_id: string
  url: string
  role: string
  count_flow: boolean
  direction: string
  virtual_line: VirtualLine
}

export interface EdgeBoxConfig {
  device_id: number
  device_name: string
  report_endpoint: string
  report_interval_s: number
  online_refresh_interval_s: number
  config_poll_s: number
  retention_days: number
  det_thresh: number
  recog_thresh: number
  verify_thresh: number
  liveness_enabled: boolean
  staff_enabled: boolean
  store_id: number
  max_frames: number
  device_psk?: string
  web_enabled: boolean
  web_port: number
  web_username: string
  web_password?: string
  cameras: CameraCfg[]
}

export interface CameraStatus {
  camera_id: string
  opened: boolean
  last_frame_ok: boolean
  frames: number
  flow_in: number
  flow_out: number
}

export interface EdgeBoxStatus {
  device_id: number
  backend: string
  version: string
  started_at: number
  uptime_s: number
  sync_version: number
  cameras: CameraStatus[]
}
// 最近抓拍记录（GET /api/snapshots）
export interface Snapshot {
  track_id: string
  camera_id: string
  created_at: number
  customer_id: number | null
  person_type: number
  similarity: number
  direction: number
  snapshot_mime: string
  snapshot: string // base64 图片（data 部分）
}
