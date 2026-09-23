import { del, get, post, put } from './client'
import type {
  Customer,
  Device,
  FlowRow,
  MatchRecord,
  PageResult,
  StaffRow,
  VisitStats,
} from './types'

// ---- 设备 ----
// 后端返回 {items:[...]}，此处解包为数组（Dashboard/Devices 均按数组使用）
export const fetchDeviceTree = async () => {
  const data = await get<{ items: Device[] }>('/devices')
  return data.items
}
export const fetchDeviceConfig = (id: number) =>
  get<{ device_id: number; config: Record<string, unknown> | null }>(`/devices/${id}/config`)
export const pushDeviceConfig = (id: number, config: Record<string, unknown>) =>
  put<{ device_id: number }>(`/devices/${id}/config`, { config })

export const updateDevice = (id: number, body: Record<string, unknown>) =>
  put<{ device_id: number }>(`/devices/${id}`, body)

// ---- 人员库 ----
export const fetchCustomers = (params: {
  page?: number
  page_size?: number
  person_type?: number
  name?: string
  status?: number
}) => get<PageResult<Customer>>('/customers', params)

export const createCustomer = (body: Record<string, unknown>) =>
  post<{ customer_id: number; version: number; history?: VisitStats }>('/customers', body)

export const updateCustomer = (id: number, body: Record<string, unknown>) =>
  put<{ customer_id: number; version: number }>(`/customers/${id}`, body)

export const deleteCustomer = (id: number, extra?: { staff?: boolean }) =>
  del<{ customer_id: number }>(`/customers/${id}`, extra)

export const appendCustomerFeature = (id: number, faceFeature: string) =>
  post<{ feature_id: number }>(`/customers/${id}/features`, { face_feature: faceFeature })

// ---- 历史来访回查 ----
export const historySearch = (body: {
  face_feature: string
  similarity_threshold?: number
  top_k?: number
  scope?: { store_ids?: number[]; start_at?: string; end_at?: string }
}) =>
  post<{
    total_visits: number
    visit_days: number
    first_visit_at: string
    last_visit_at: string
    matched_records: MatchRecord[]
  }>('/history/search', body)

// ---- 统计 ----
export const fetchFlowStats = (params: {
  granularity?: string
  person_type?: number
  start_at: string
  end_at?: string
}) => get<{ items: FlowRow[]; total: { in: number; out: number } }>('/stats/flow', params)

export const fetchStaffStats = (params: { start_at: string; end_at?: string }) =>
  get<{ items: StaffRow[] }>('/stats/staff', params)

export const fetchVisitStats = (customerId: number) => get<VisitStats>(`/stats/visits/${customerId}`)