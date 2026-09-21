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
  last_heartbeat: number
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
  name: string
  id_card_no?: string
  staff_no?: string
  department?: string
  gender?: number
  birth_date?: number
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