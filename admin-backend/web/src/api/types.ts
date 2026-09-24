// 与后端模型对应的 TS 类型（snake_case 与接口契约一致）

export type DeviceType = 1 | 2 | 3 | 4 | 5
export type PersonType = 0 | 1

/** 设备（含父子层级） */
export interface Device {
  id: number
  device_type: DeviceType
  parent_id: number | null
  device_key: string
  name: string
  store_id: number
  status: 0 | 1
  last_seen_at: number
  created_at: number
  children?: Device[]
}

export const DEVICE_TYPE_TEXT: Record<DeviceType, string> = {
  1: '边缘盒子',
  2: '录入电脑端',
  3: 'RTSP摄像头',
  4: 'USB摄像头',
  5: '身份证读卡器',
}

/** 人员档案（顾客 / 内部人员） */
export interface Customer {
  id: number
  person_type: PersonType
  store_id?: number
  store_name?: string
  name: string
  id_card_no?: string
  staff_no?: string
  department?: string
  gender?: number
  birth_date?: number
  address?: string
  id_photo_path?: string
  id_photo_url?: string
  live_photo_path?: string
  live_photo_url?: string
  status: number
  version: number
  created_at: number
  history?: VisitStats
}

/** 历史来访统计 */
export interface VisitStats {
  customer_id?: number
  total_visits: number
  visit_days: number
  first_visit_at: string
  last_visit_at: string
}

/** 历史回查匹配明细 */
export interface MatchRecord {
  log_id: number
  similarity: number
  created_at: string
  device_id?: number
  camera_id?: string
  direction?: number
  snapshot?: string
}

export interface PageResult<T> {
  total: number
  items: T[]
}

/** 客流统计聚合行 */
export interface FlowRow {
  bucket: string
  camera_id?: string
  in: number
  out: number
}

/** 员工通行统计 */
export interface StaffRow {
  customer_id: number
  staff_no: string
  department: string
  in: number
  out: number
  last_in: string
}
/** 门店 */
export interface Store {
  id: number
  name: string
  address?: string
  status: number
  created_at: number
  updated_at: number
}

/** 识别记录（查询） */
export interface RecognitionRecord {
  id: number
  device_id: number
  track_id: string
  customer_id?: number
  customer_name?: string
  person_type: number
  similarity: number
  direction: number
  camera_id?: string
  created_at: string
  snapshot?: string
  snapshot_url?: string
  snapshot_mime?: string
}

/** 人证核验记录（查询） */
export interface VerifyRecord {
  id: number
  customer_id: number
  verify_result: number
  similarity: number
  liveness_score: number
  device_id: number
  operator?: string
  created_at: string
  live_photo?: string
  live_photo_url?: string
}
/** 人员导入失败明细 */
export interface ImportFailRow {
  row: number
  name: string
  reason: string
}

/** 人员导入结果 */
export interface ImportResult {
  total: number
  success: number
  failed: ImportFailRow[]
}

/** 全局系统配置 */
export interface SystemConfig {
  face_service_url: string
  face_service_key_set: boolean
  effective_url: string
  effective_key_used: boolean
  face_service_default: string
}